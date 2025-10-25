# Async Architecture

Async architecture of slangd using ASIO coroutines for non-blocking LSP operations.

---

## System Overview

```
JSON-RPC → LSP Handlers → Language Service → Session Manager → Compilation Pool
           (executor_)    (executor_)        (session_strand_)  (compilation_pool_)
```

**Key insight**: Main thread handles protocol coordination, background threads handle computation.

---

## Executors

### executor\_ - Main LSP Executor

- Single-threaded io_context for all LSP protocol handling
- Coroutines enable concurrency (handlers suspend while others run)
- Safe to `co_await` (suspends current handler, allows others to execute)

### session_strand\_ - Serialization Strand

- Protects shared mutable state (document maps, session storage, timers)
- Executes tasks one at a time (serialized access)
- **Critical constraint**: Only O(1) operations allowed (no `co_await` slow operations)
- Pattern: Enter → Access → Exit immediately

### compilation_pool\_ - CPU Work Pool

- Background thread pool for compilation and semantic indexing
- Isolated from LSP handlers (no shared state)
- Work spawned here via `asio::co_spawn`

---

## When, Why, and How to Synchronize

### When Do We Need Synchronization?

1. **Multiple coroutines accessing shared mutable state**

   - Document tracking (open files, versions, content)
   - Session storage (sessions\_, pending\_, cleanup_timers\_)

2. **Coordinating async work completion**

   - Waiting for compilation before extracting diagnostics
   - Waiting for indexing before serving go-to-definition
   - Notifying multiple waiters when session becomes ready

3. **Cancelling stale work**
   - User saves v6 while v5 compiling → cancel v5, start v6

4. **Workspace initialization ordering**
   - LSP requests arrive before workspace infrastructure ready
   - Must wait for preamble and session manager to exist

### Why Do We Need Synchronization?

**Without synchronization → race conditions:**

- Two coroutines access map simultaneously → undefined behavior

**Without coordination → wasted work:**

- Save v5 → compile (8s), save v6 → compile (8s overlapping) → v5 wasted

**Without notification → polling overhead:**

- Busy-wait for completion wastes CPU and adds latency

### How Do We Achieve Synchronization?

**1. Strand** - Serialize access to shared state

- **When**: Accessing document/session storage maps
- **Pattern**: `co_await post(strand_)` → O(1) access → `co_await post(executor_)`

**2. BroadcastEvent** - Notify multiple waiters when work completes

- **When**: Waiting for compilation/indexing without blocking
- **Pattern**: Producer sets event → All consumers wake and check storage
- **Key property**: True broadcast (all waiters notified, late joiners complete immediately)

**3. Cancellation** - Stop stale work

- **When**: New document version invalidates ongoing work
- **Pattern**: `atomic<bool> cancelled` flag checked periodically during work

**4. Initialization Event** - Wait for workspace ready

- **When**: LSP operations require workspace infrastructure (preamble, session manager)
- **Pattern**: All public methods wait on workspace_ready event before proceeding
- **Key property**: Blocks only during initial startup, not during preamble rebuilds

---

## Synchronization Mechanisms

### BroadcastEvent for Multi-Waiter Notification

**Problem with ASIO channels**: One-shot delivery (first waiter gets value, others don't).

**Solution**: Custom BroadcastEvent primitive with true broadcast semantics:

- All current waiters notified simultaneously
- Late joiners complete immediately if already set
- Lightweight (no data storage, pure notification)

**Pattern** (notification + storage):

```
Producer:
  1. Store data in session storage
  2. Set BroadcastEvent

Consumer:
  1. AsyncWait on BroadcastEvent
  2. Check session storage for data
```

**Why separation**: Storage is source of truth, events are notifications. Avoids data transfer overhead through event mechanism.

### Two-Phase Session Creation

**Phase 1 - Elaboration Complete** (~126ms):

- Compilation and diagnostics available
- `compilation_ready` event fires
- Diagnostic extraction hooks execute

**Phase 2 - Indexing Complete** (~455ms):

- Semantic index built (symbols, definitions, references)
- `session_ready` event fires
- LSP features (go-to-def, symbols) can proceed

**Benefits**:

- Fast diagnostics (don't wait for indexing)
- Multiple waiters notified simultaneously (via BroadcastEvent)
- Hooks guarantee execution before storage (server-push features)

### Cancellation Semantics

**Problem**: User rapidly saves → pending sessions get cancelled.

**Solution**: Fail fast with error (no retry logic)

- Cancelled request returns error immediately (~1ms)
- Client sends new request with current context
- Matches LSP server conventions (clangd, rust-analyzer)
- Return error (not empty) to prevent UI flicker

**Session replacement pattern**:

```
Create new pending → Cancel old → Replace atomically (no gap)
```

### Workspace Initialization Synchronization

**Problem**: LSP requests arrive before workspace infrastructure exists (preamble, session manager).

**Solution**: LanguageService maintains workspace_ready event. All public methods wait for this event before proceeding.

**Behavior**:

- Initial startup: Event unset, requests wait until InitializeWorkspace completes
- After initialization: Event set permanently, AsyncWait returns immediately
- Preamble rebuilds: Event stays set, no blocking (old preamble remains available)

**Key distinction**: Initial initialization has nothing available (must wait). Preamble rebuilds have old data available (continue serving with current data, swap atomically when ready).

**Benefits**:

- Eliminates lifecycle state checks and nullptr guards throughout codebase
- No manual pending request tracking or catch-up loops required
- Requests never receive errors due to initialization timing
- Single synchronization point instead of distributed guards

---

## Handler Patterns

### Notifications (OnDidSave, OnDidChange)

**Pattern**: Return immediately, spawn background work

```
OnNotification():
  post(strand_) → get document data → post(executor_)
  co_spawn(background_work, detached)  # Don't wait
  co_return  # Handler finishes immediately
```

### Requests (OnDocumentSymbols, OnGotoDefinition)

**Pattern**: Can wait for results

```
OnRequest():
  session = co_await GetSession(uri)  # Waits if needed
  co_return session.ComputeFeature()
```

**Key difference**: Requests run on executor\_ where waiting is safe (other handlers continue).

---

## Strand Usage Pattern

**Rule**: Enter → Fast O(1) Access → Exit

**CORRECT**:

```
co_await post(strand_)          # Enter
file = GetOpenFile(uri)         # O(1) lookup
version = file.version
co_await post(executor_)        # Exit immediately
```

**WRONG - Blocks all handlers**:

```
co_await post(strand_)
session = co_await GetSession(uri)  # Blocks strand for seconds!
```

**Why this matters**: Strand is single-threaded. One blocking coroutine freezes all handlers waiting for shared state access.

---

## Data Flow Example: OnDidSave

```
1. JSON-RPC receives didSave notification
2. Spawn handler (detached on executor_)
3. Handler: strand_ → get content → executor_ → spawn background → return
4. Background: UpdateSession spawns compilation to compilation_pool_
5. Compilation completes → store session → set compilation_ready event
6. Diagnostic extraction (waiting on event) wakes → extracts → publishes
7. Indexing completes → set session_ready event
8. LSP feature requests (waiting on event) wake → serve features
```

**Key points**:

- Handler returns at step 3 (non-blocking)
- Compilation runs isolated on pool (step 4-5)
- Events signal when ready (steps 5, 7)
- Multiple waiters wake simultaneously (broadcast)

---

## Architecture Invariants

1. **Strand never blocked** - Only O(1) operations while on strand
2. **Handlers on executor\_** - Single-threaded, but coroutines enable concurrency
3. **Heavy work on compilation_pool\_** - CPU-bound operations isolated
4. **BroadcastEvents for coordination** - No polling or busy-waiting
5. **Cancellation for long work** - Atomic flag checked periodically
6. **Errors for cancelled sessions** - Return error (not empty) to prevent UI flicker

**Violation detection**:

- Strand blocking → All handlers freeze (8+ second delays)
- Missing cancellation → CPU waste compiling old versions
- Returning empty on cancellation → UI flicker (symbols disappear/reappear)
