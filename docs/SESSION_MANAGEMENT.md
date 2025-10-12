# Session Management

## Overview

SessionManager controls the lifecycle of OverlaySession instances - from compilation through caching to eviction. Each session represents a complete SystemVerilog compilation (AST + semantic index) for a single file.

**Core responsibility**: Provide sessions to LSP features while maintaining bounded memory usage.

## Memory Constraints

Sessions are memory-intensive (complete compilation + semantic index). The cache limit (default 8 active sessions) balances responsiveness with memory bounds.

**Design constraint**: Sessions must be evictable. Users with more open files than cache capacity will experience recompilation when switching tabs.

## Component Architecture

### OpenDocumentTracker (Single Source of Truth)

**Purpose**: Tracks which documents are currently open in the editor.

**Implementation**: Simple `std::unordered_set<std::string>` with synchronous operations.

**Composition pattern**:

```
LanguageService creates and owns OpenDocumentTracker
├── DocumentStateManager composes tracker → Updates on open/close
└── SessionManager composes tracker → Queries for eviction decisions
```

**Why separate component?**

- Prevents state duplication between DocumentStateManager and SessionManager
- Both managers need "is document open?" but for different purposes:
  - DocumentStateManager: Maintain content validity
  - SessionManager: Prioritize eviction of closed files
- Single source of truth via shared reference (not coupling)

**What it does NOT do**:

- No document content (that's DocumentStateManager)
- No session management (that's SessionManager)
- Just open/closed state tracking

### Session State Transitions

1. **Pending** (`pending_sessions_`): Compilation in progress on background thread
2. **Active** (`active_sessions_`): Compilation complete, cached for reuse (LRU eviction)
3. **Evicted**: Removed from cache, feature requests fail gracefully (client can retry)

## VSCode Behavioral Patterns (Critical Design Context)

Understanding these patterns is essential to the design decisions below.

### Pattern 1: Prefetch on Hover

**Sequence**:

```
1. User hovers symbol 'foo' in file A
2. Client sends: textDocument/definition (file A, position of 'foo')
3. Server responds: "Definition at file B, line 50"
4. VSCode optimization: didOpen(B) + didClose(B) [within 50-100ms]
5. File B never appears in editor UI
6. Purpose: Pre-cache file B for instant navigation if user clicks
```

**Why this matters**: The didOpen → didClose sequence happens before any feature request for file B. If we cancel pending compilations on close, we waste the prefetch.

### Pattern 2: Preview Mode Navigation

**Sequence**:

```
1. User single-clicks file1 in file explorer (preview tab, italicized)
2. Client sends: didOpen(file1)
3. User single-clicks file2 → VSCode auto-closes preview tab
4. Client sends: didClose(file1) + didOpen(file2)
5. Repeat 100 times → 100 didOpen/didClose pairs in rapid succession
```

**Why this matters**: This is the actual memory problem we're solving. Without cancellation, pending compilations accumulate unboundedly causing memory exhaustion.

**Key distinction**: Pattern 1 is quick reopen (prefetch optimization), Pattern 2 is never reopen (user exploring files).

## Design Decisions & Tradeoffs

### Decision 1: Cancel Pending on Close

**Implementation**: When `OnDocumentClosed()` called, immediately cancel pending compilation via `CancelPendingSession()`.

**Tradeoff analysis**:

**Impact on prefetch pattern (hover → open B → close B → click → open B)**:

- First hover: Pending cancelled on close → user clicks → recompile → **2 second wait**
- Subsequent hovers: Session cached after first completion → **instant**
- UX: First interaction slightly slower, then cached

**Impact on preview mode spam (100 rapid open/close)**:

- Without cancellation: Unbounded pending compilations → memory explosion
- With cancellation: Limited concurrent compilations → bounded memory
- UX: Prevents server crashes

**Why this tradeoff is correct**:

1. Prefetch slowdown: Affects first interaction only, then cached
2. Memory explosion: Unbounded growth causes server crashes
3. User expectation: Slight delay on first navigation is acceptable LSP behavior

**Alternative considered and rejected**: Grace period timers (delay cancellation by 1-2 seconds)

**Why rejected**:

- Non-deterministic behavior (depends on timing, CPU speed)
- Hard to test (requires time-based assertions, flaky tests)
- Race conditions (what if user reopens during grace period? timer cleanup?)
- Complexity unjustified (minor UX improvement not worth maintenance burden)

**Design principle**: Prefer simple, predictable, deterministic behavior over complex optimizations.

### Decision 2: Eviction Priority for Active Sessions

**Policy**: When cache exceeds limit, evict in this priority order:

1. **Closed files** (not in `open_documents_`): Evict by LRU
2. **Open files** (in `open_documents_`): Evict by LRU only when forced

**Rationale**: Closed files are prefetch cache (nice-to-have), open files are actively used (essential).

### Decision 3: Separate OpenDocumentTracker

**Alternative considered**: SessionManager directly queries DocumentStateManager for open state.

**Why separate component**:

- Avoids coupling: SessionManager doesn't need to know about document content
- Clean responsibility: OpenDocumentTracker = state, DocumentStateManager = content + state management
- Composition over coupling: Both managers compose the same tracker
- Testability: Can mock OpenDocumentTracker without DocumentStateManager

**Design principle**: Prefer composition with single source of truth over cross-component queries.

### Decision 4: No Rebuild on Eviction

**Policy**: LSP feature requests fail gracefully if session not found (evicted or not yet created).

**Rationale**:
- Eviction means session wasn't important enough to keep in bounded cache
- Client will retry if user needs it (e.g., reopening file, navigating back)
- Simpler code - no rebuild-and-retry logic in feature layer
- Respects SessionManager lifecycle decisions - features are read-only consumers

**Alternative considered**: Auto-rebuild on first access after eviction

**Why rejected**:
- Violates separation of concerns (features shouldn't trigger lifecycle operations)
- Blocks feature requests on 2-second recompilation (poor UX)
- Undermines eviction decisions made by SessionManager

## Request Patterns

### WithSession - Dependency Inversion for Feature Extraction

**Problem**: How do LSP features access sessions without holding shared_ptr (causing memory leaks)?

**Solution**: Dependency inversion - SessionManager executes feature code WITH the session, rather than giving session TO feature code.

**API** (template methods, only way to access sessions):
```
WithSession(uri, callback) -> expected<Result, error>
WithCompilationState(uri, callback) -> expected<Result, error>
```

**Pattern**:
```
WithSession(uri, callback):
  acquire strand
  find session in cache (or wait for pending creation)
  execute callback(session) synchronously on strand
  release strand
  return result or error

Feature example:
  result = WithSession(uri, [](session) { return session.GetDocumentSymbols() })
  if (!result) return error_to_client
  return *result
```

**Why this works**:

- **No shared_ptr escape**: Callback gets const reference, not ownership
- **Strand as lock**: While callback runs on strand, eviction cannot proceed
- **Separation preserved**: SessionManager controls lifecycle, LanguageService implements features
- **Simple**: No manual refcounting, no in_use_count tracking
- **Graceful failure**: Returns error if session not found (evicted/cancelled) - no rebuild

**Memory bound**: Strand serializes all operations (create/evict/extract). Only one extraction at a time, so at most:

- 8 cached sessions
- 4 building in pool
- **12 sessions max (4.2GB)**

### Multi-Waiter Coordination (Broadcast Events)

**Problem**: Multiple feature requests arrive for same file while compilation pending. How to avoid duplicate work?

**Solution**: Two-phase broadcast events (`compilation_ready`, `session_ready`) provide pure notification.

**Pattern**:

```
Request 1: WithSession(uri, callback) → Not in cache → Wait on pending->session_ready
Request 2: WithSession(uri, callback) → Not in cache → Wait on same pending->session_ready
Request 3: WithSession(uri, callback) → Not in cache → Wait on same pending->session_ready

Compilation completes:
  1. Store session in active_sessions_ cache
  2. Broadcast event.Set() (wakes ALL waiters simultaneously)
  3. All waiters re-acquire strand, check cache, execute callbacks
```

**Key insight**: Broadcast events provide pure notification (no data transfer). Store in cache first, then broadcast.

**Why this works**: Cache-first pattern eliminates convoy effect at strand serialization point. Background thread completes once, multiple requesters share result via cache.

## State Consistency Guarantees

### Strand Serialization

**All state mutations protected by `session_strand_`**:

- Adding/removing pending sessions
- Adding/removing active sessions
- Updating LRU access order
- Querying OpenDocumentTracker

**Consequence**: No data races, all state transitions are atomic from external perspective.

### Version Tracking

**Cache key**: URI + LSP document version (not content hash)

**Version mismatch handling**:

```
UpdateSession(uri, content, version):
  if pending_sessions_[uri].version != version:
    Cancel old pending (close channels)
    Start new compilation
```

**Why**: LSP provides version tracking for free, no need to hash content. Version increments on every edit, ensuring cache invalidation.

### Cancellation

**Erasing pending sessions signals cancellation**:

```
Background compilation:
  // Check if still current before storing
  auto it = pending_sessions_.find(uri);
  if (it == pending_sessions_.end() || it->second->version != pending->version):
    // Cancelled, abort early
    return
```

**Why**: Background threads check pending state at checkpoints, can abort expensive work early if result no longer needed.

## Memory Bounds Analysis

**With cancellation**: Pending compilations bounded by user interaction speed (typically 2-3 concurrent)

**Without cancellation**: Preview mode spam causes unbounded pending accumulation → memory exhaustion

## Testing Considerations

**What makes this design testable**:

1. **Deterministic state transitions**: No timers, no race conditions
2. **Mockable OpenDocumentTracker**: Can simulate open/close patterns
3. **Synchronous queries**: `Contains(uri)` is immediate, no async complexity
4. **Observable state**: Can inspect `pending_sessions_.size()` and `active_sessions_.size()`
5. **Broadcast observable**: Check `event.IsSet()` to verify notification delivered

**Key test scenarios**:

- Rapid open/close: Verify pending bounded (not accumulating)
- LRU eviction: Open 10 files, verify only 8 cached, verify LRU order
- Reopen evicted: Verify recompilation triggered, session restored
- Version mismatch: Verify old pending cancelled, new compilation started
- Multi-waiter: Verify multiple GetSession() calls share single compilation
- Broadcast notification: Verify all waiters wake on Set(), late joiners complete immediately

## Future Optimizations

**Memory reduction**: Lower per-session cost → increase cache limit → less eviction thrashing

**Persistent indexing**: Sessions become views into global index → dramatically lower memory per session

**Architecture designed for evolution**: Abstract interfaces enable performance improvements without breaking LSP layer.
