# Async Architecture

This document describes the asynchronous architecture of slangd.

---

## System Overview

**Flow:** JSON-RPC reads messages → spawns handlers → handlers wait for sessions → sessions compile on pool

---

## Executors

### `executor_` - Main LSP Executor (single-threaded, from `io_context`)

- All LSP handlers run here by default
- Single-threaded, but coroutines enable concurrency (one handler can suspend while others run)
- Safe to `co_await` (suspends current coroutine, allows other handlers to execute)

### `strand_` - Serialization Strand (derived from `executor_`)

- Protects shared mutable state: `open_files_` map
- Executes tasks one at a time (serialized)
- **Critical constraint:** Only O(1) operations allowed (no `co_await` slow operations)
- **Note:** Transport layer has its own strand for message ordering - application strand only needed for local state

### `compilation_pool_` - CPU Work Pool (4 threads, separate `thread_pool`)

- CPU-intensive compilation and semantic indexing
- Isolated from LSP handlers
- SessionManager spawns work here

---

## Layer Architecture

### Layer 1: JSON-RPC Endpoint

**Location:** `/home/hankhsu/workspace/c++/jsonrpc-cpp-lib/src/endpoint/endpoint.cpp`

**Pattern:** Concurrent message reading

```cpp
while (is_running_) {
  auto message = co_await transport_->ReceiveMessage();

  // Spawn handler concurrently - don't block read loop
  asio::co_spawn(executor_, HandleMessage(message), asio::detached);
}
```

**Key characteristic:** Each message handler runs independently. Loop immediately reads next message.

### Layer 2: LSP Server (SlangdLspServer)

**Location:** `src/slangd/core/slangd_lsp_server.cpp`

**Responsibilities:**

- Dispatch LSP requests/notifications to handlers
- Manage shared state via `strand_`
- Spawn background work

**Executor usage:**

- Handlers execute on `executor_` (default context)
- Shared state access via `strand_` (enter/exit pattern)
- Background work spawned to `executor_` with `asio::detached`

### Layer 3: Language Service

**Location:** `src/slangd/services/language_service.cpp`

**Responsibilities:**

- Facade for LSP features
- Coordinate between SessionManager and feature implementations
- Spawn compilation pool work when needed

**Executor usage:**

- Runs on `executor_` (called from LSP layer)
- Spawns to `compilation_pool_` for PreambleManager builds
- Waits for SessionManager async operations via `co_await`

### Layer 4: Session Manager

**Location:** `src/slangd/services/session_manager.cpp`

**Responsibilities:**

- Session lifecycle: create, cache, invalidate
- Spawn compilation/indexing to `compilation_pool_`
- Signal completion via ASIO channels

**Executor usage:**

- Public API runs on `executor_`
- Spawns compilation work to `compilation_pool_`
- Uses channels for async coordination between layers

---

## Handler Patterns

### Notifications (OnDidSave, OnDidChange, etc.)

Pattern: Return immediately, spawn background work

```cpp
auto OnNotification(...) -> asio::awaitable<void> {
  // 1. Access shared state via strand
  co_await asio::post(strand_, asio::use_awaitable);
  auto [content, version] = GetFileData(uri);
  co_await asio::post(executor_, asio::use_awaitable);

  // 2. Spawn background work (detached)
  asio::co_spawn(
      executor_,
      [=]() -> asio::awaitable<void> {
        co_await language_service_->OnDocumentSaved(...);
        co_await ProcessDiagnostics(uri);
      },
      asio::detached);

  // 3. Return immediately (don't wait for background work)
  co_return Ok();
}
```

### Requests (OnDocumentSymbols, OnGotoDefinition, etc.)

Pattern: Can wait for results (runs on executor\_)

```cpp
auto OnRequest(...) -> asio::awaitable<Result> {
  // Already on executor_ - no strand needed if no shared state access
  auto session = co_await language_service_->GetSession(uri);
  co_return session->ComputeFeature();
}
```

**Key difference:** Requests can `co_await` because they run on `executor_` where waiting is safe (other handlers run concurrently on other threads).

---

## Synchronization Mechanisms

### 1. Channels (Session Ready Signals)

**Two-phase creation:**

```cpp
struct PendingCreation {
  channel<CompilationState> compilation_ready;  // Phase 1: ~126ms
  channel<OverlaySession> session_ready;        // Phase 2: ~455ms
};
```

**Usage:**

- Diagnostics: `co_await compilation_ready->async_receive()` (fast path)
- Document symbols: `co_await session_ready->async_receive()` (full session)

**Why channels:** Suspend coroutine without blocking thread. Other handlers continue executing.

**Channel Usage Pattern:**

ASIO channels are single-producer, single-consumer (one-shot delivery). For multi-consumer scenarios, we use the **notification + cache pattern**.

**Alternatives avoided:**
- Custom broadcast mechanism (100+ lines, complex state)
- Retry logic (wasteful, confusing semantics)
- Multiple channels per waiter (memory overhead)

**Design insight:** Channels are for efficient wake-up, cache is for data storage. Work with ASIO's design (one-shot channels), not against it.

```cpp
// Pattern: Channels notify, cache provides data
auto GetSession(uri) -> asio::awaitable<Session> {
  // 1. Fast path: Check cache (source of truth)
  if (active_sessions_.contains(uri)) return active_sessions_[uri];

  // 2. Slow path: Wait for notification
  if (pending_sessions_.contains(uri)) {
    auto result = co_await channel->async_receive(ec);
    if (!ec) return result;  // First waiter: got value directly

    // 3. Re-check cache after notification (other waiters)
    if (active_sessions_.contains(uri)) return active_sessions_[uri];

    // 4. Not in cache - truly cancelled
    return nullptr;
  }
}
```

**Why this works:**
- **First waiter:** Gets value from channel (optimization - avoids cache write/read)
- **Subsequent waiters:** Wake when channel closes → check cache (populated by background task)
- **Cancelled sessions:** Not in cache → all waiters correctly get nullptr

**Key insight:** Channels are notification mechanisms, not data storage. Cache is source of truth.

This pattern is common in async systems (futex, pub/sub, database notifications).

### 2. GetSession() / GetCompilationState()

Wait for session creation without blocking:

```cpp
// Runs on executor_ - safe to wait
auto session = co_await session_manager_->GetSession(uri);

// Implementation uses notification + cache pattern (see above)
```

### 3. Cancellation Signals

**Purpose:** Discard stale work when new document version arrives

**SessionManager pattern:**

```cpp
// Store signal with pending session
auto pending = make_shared<PendingCreation>();
pending->cancellation = make_shared<asio::cancellation_signal>();

// Bind to spawned work
asio::co_spawn(
    compilation_pool_->get_executor(),
    Work(slot),
    asio::bind_cancellation_slot(pending->cancellation->slot(), asio::use_awaitable));

// On new version: emit cancellation
if (old_pending) {
  old_pending->cancellation->emit(asio::cancellation_type::terminal);
}
```

**SemanticIndex pattern:**

```cpp
// Check cancellation at strategic points
if (IsCancelled(cancellation_slot)) {
  return nullptr;  // Abort work, return to caller
}
```

---

## Strand Usage Pattern

**Rule:** Enter → Fast Access → Exit

```cpp
// ✅ CORRECT
co_await asio::post(strand_, asio::use_awaitable);  // Enter
auto file = GetOpenFile(uri);                        // O(1) map lookup
auto version = file.version;
co_await asio::post(executor_, asio::use_awaitable); // Exit immediately

// ❌ WRONG: Blocks all handlers
co_await asio::post(strand_, asio::use_awaitable);
auto session = co_await GetSession(uri);  // Blocks strand for seconds!
```

**Why this matters:** Strand is single-threaded. If one coroutine blocks on strand, ALL handlers waiting to access shared state are frozen.

---

## Data Flow Example: OnDidSave

```
1. JSON-RPC receives "textDocument/didSave"
   ↓
2. Spawns OnDidSave handler (detached on executor_)
   ↓
3. Handler posts to strand_ → gets file content → posts back to executor_
   ↓
4. Handler spawns background work (detached) and returns
   ↓ (background continues)
5. Language service updates PreambleManager (spawned to compilation_pool_)
   ↓
6. SessionManager::UpdateSession()
   - Cancels old pending session (emit signal)
   - Spawns new compilation to compilation_pool_
   ↓
7. Compilation completes → signals compilation_ready channel
   ↓
8. ProcessDiagnostics (waiting on channel) receives signal
   ↓
9. Extracts diagnostics from CompilationState → publishes to client
```

**Key points:**

- Handler returns at step 4 (doesn't wait)
- Compilation runs on isolated pool (step 6)
- Channel signals when ready (step 7)
- No polling or blocking

---

## Executor Selection Rules

```
Task Type                 → Executor
─────────────────────────────────────────
LSP handler (default)     → executor_
Shared state access       → strand_
Compilation/indexing      → compilation_pool_
Background work           → executor_ (detached spawn)
```

---

## Session Cancellation Semantics

### Problem: Cancelled Sessions

When user rapidly saves/edits, pending sessions get cancelled:

```
t=0: Save v5 → pending_sessions_[uri] = v5_pending
t=1: GetDocSymbol waits on v5_pending->channel
t=2: Save v6 → cancels v5, creates v6_pending
t=3: GetDocSymbol gets channel error
     → What to do? Retry for v6? Return empty? Return error?
```

### Solution: Fail Fast with Error

**No retry logic** - requests fail immediately when session cancelled:

```cpp
auto GetSession(uri) {
  if (cached) return cached;

  if (pending) {
    auto session = co_await pending->channel->receive(ec);
    if (!ec) return session;
    // Channel error → cancelled → return nullptr (no retry!)
  }

  return nullptr;  // Caller handles failure
}
```

**Return error (not empty)** to prevent UI flicker:

```cpp
auto GetDocumentSymbols(uri) {
  auto session = co_await GetSession(uri);

  if (!session) {
    // Return ERROR → client keeps old symbols visible (no flicker!)
    return UnexpectedFromCode(kInternalError, "Document was modified");
  }

  return session->GetSymbols();
}
```

### Why No Retry?

**Problem with retry logic:**

- Old request (for v5) waits 8+ seconds for v6 to compile
- User already moved on to v7 by then
- Client doesn't know request was "upgraded"

**Why fail-fast is better:**

- Request fails immediately (~1ms)
- Client sends new request with current context
- Clear semantics: cancelled = error
- Matches clangd/rust-analyzer behavior

### Session Replacement Pattern

**Critical:** Create new BEFORE cancelling old (no gap):

```cpp
// ✅ CORRECT - no gap
auto new_pending = StartSessionCreation(uri, content, version);
if (old_pending) {
  old_pending->cancel();
}
pending_sessions_[uri] = new_pending;  // Atomic replacement

// ❌ WRONG - gap allows race condition
pending_sessions_.erase(uri);  // Remove old
// GAP: GetSession retry finds nothing!
pending_sessions_[uri] = new_pending;  // Add new
```

### Error Propagation

```
GetSession → nullptr
  ↓
GetDocumentSymbols → UnexpectedFromCode(kInternalError, "Document was modified")
  ↓
OnDocumentSymbols → forwards error
  ↓
LSP client → keeps old symbols visible (no flicker!)
```

---

## Architecture Invariants

1. **Strand is never blocked** - Only O(1) operations while on strand
2. **Handlers run on executor\_** - Single-threaded io_context, but coroutines allow concurrency
3. **Heavy work on compilation_pool\_** - CPU-bound operations isolated from LSP handlers
4. **Channels for synchronization** - No polling or busy-waiting
5. **Cancellation for long work** - New document versions cancel stale compilation
6. **Errors for cancelled sessions** - Return error (not empty) to prevent UI flicker

**Violation detection:**

- Strand blocking → All handlers freeze (8+ second delays)
- Missing cancellation → CPU waste compiling old versions
- Returning empty on cancellation → UI flicker (symbols disappear/reappear)
