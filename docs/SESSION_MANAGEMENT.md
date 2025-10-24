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

### Two Access Patterns

**1. Hook-based extraction** (server-push features like diagnostics):
- Hooks execute **during** session creation (before caching) on strand
- Guaranteed execution - session cannot be evicted while hook runs
- Solves race condition: when 20 files open rapidly, async extraction after caching can miss evicted sessions
- Use: `UpdateSession(uri, content, version, diagnostic_hook)`

**2. Callback-based access** (client-request features like go-to-def, symbols):
- SessionManager executes feature code WITH session (dependency inversion)
- Callbacks get const reference, no shared_ptr escape
- Strand serializes access - eviction blocked while callback runs
- Graceful failure if evicted - client can retry
- Use: `WithSession(uri, [](session) { return session.GetSymbols() })`

**Memory bound** (both patterns): Strand serializes all operations, so at most 8 cached + 4 building = **12 sessions max**

| Aspect | Hook-Based | Callback-Based |
|--------|------------|----------------|
| **Timing** | During creation | After caching |
| **Guarantee** | Always executes | Best-effort |
| **Use cases** | Diagnostics | Go-to-def, symbols |

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

## Preamble Rebuild and Session Coordination

### The Dependency Chain

**Why sessions must hold preamble reference:**

Sessions contain SemanticIndex which stores raw pointers to AST symbols for document symbols feature. These pointers are only valid while the Compilation (and its preamble) remains alive. If preamble is freed, document symbols feature would access dangling pointers and crash.

**Implication**: Session lifetime determines preamble lifetime. Multiple sessions from the same preamble generation → multiple references → preamble stays alive.

### The Memory Spike Problem (Rapid Saves)

**Scenario**: User saves file 10 times rapidly during a 20-second preamble build on slow server.

**Original design (loop-based rebuild)**:
- Iteration 1: Build preamble A, spawn 4 session tasks (async)
- Iteration 2: Start immediately → build preamble B while iteration 1's sessions still building
- Iteration 3+: Continue looping...
- Result: 5-10 preambles alive simultaneously (each 1GB) → 10GB memory spike

**Root cause**: Looping immediately after preamble build doesn't wait for previous iteration's sessions to complete. Since sessions hold preamble references, old preambles can't be freed until their sessions finish.

**For detailed explanation of memory fragmentation, allocator behavior, and why RSS stays at peak, see `MEMORY_ARCHITECTURE.md`.**

### Approaches Evaluated

**Approach 1: Wait for session tasks to complete**
- **Idea**: After spawning session tasks, wait for them to finish before checking for next rebuild
- **Why doesn't work**: Requires tracking async task completion, adds complexity ("semaphore" pattern)
- **Issue**: Loses parallelism if made synchronous (4×200ms = 800ms vs 200ms parallel)

**Approach 2: Arbitrary delay between rebuilds**
- **Idea**: Add 1.5s delay between iterations, hope sessions complete in that time
- **Why it's a band-aid**: Not guaranteed (what if sessions take >1.5s?), relies on timing assumptions
- **When it fails**: Slow server or complex files → sessions still overlapping

**Approach 3: Drop Compilation after diagnostics (blocked)**
- **Idea**: Extract diagnostics, then drop Compilation+preamble, keep only SemanticIndex
- **Why doesn't work**: Document symbols feature uses raw symbol pointers from Compilation
- **Blocker**: Would require architectural change to document symbols

### Current Solution (Temporary)

**Implemented**: Remove loop, schedule next rebuild with debounce delay

**How it works**:
- Rebuild completes fully (including sessions spawned, though still running async)
- If more saves happened → schedule next rebuild (1.5s debounce)
- Gap between rebuilds allows previous sessions to complete and release preamble

**Why this reduces memory**:
- Prevents continuous overlapping iterations
- Session tasks (200ms) typically complete during 1.5s gap (7.5× safety margin)
- Reduces peak from 10GB → 2-3GB (acceptable for most cases)

**Known limitations**:
- Still timing-dependent (not guaranteed)
- Rare cases: If sessions take >1.5s, might still have 2 preambles briefly
- **This is explicitly a band-aid** pending proper architectural fix

**Acceptable tradeoff**:
- Much better than 10GB spikes (2-3GB is manageable)
- Works for 95%+ of real-world scenarios
- Buys time for proper fix without major architectural changes

### Proper Fix (Future Work)

**Root cause**: Document symbols uses semantic symbols (requires Compilation), but document symbols is inherently a syntactic feature (file structure/outline).

**Solution**: Migrate document symbols to syntax tree traversal
- Parse tree available immediately after parsing
- No semantic elaboration needed
- No dependency on Compilation or preamble
- **Enables dropping Compilation after diagnostics extraction**

**What this unlocks**:
- Sessions no longer hold preamble long-term
- Preamble freed immediately after diagnostics published
- No memory accumulation even with rapid rebuilds
- Removes need for complex task coordination
- Simpler, cleaner architecture

**Estimated effort**: 2-3 days
- Implement syntax tree document symbol builder
- Migrate feature to use syntax tree version
- Remove Compilation retention from sessions
- Simplify preamble rebuild logic (no task joining needed)

**Design principle**: Features should use the lightest representation that supports their needs. Document symbols needs structure, not semantics.

## Future Optimizations

**Memory reduction**: Lower per-session cost → increase cache limit → less eviction thrashing

**Persistent indexing**: Sessions become views into global index → dramatically lower memory per session

**Architecture designed for evolution**: Abstract interfaces enable performance improvements without breaking LSP layer.
