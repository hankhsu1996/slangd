# Memory Architecture

## Overview

Slangd's memory usage is dominated by two components:

- **PreambleManager**: Shared compilation of all project files (scales with project size)
- **OverlaySessions**: Per-file compilation + semantic index (scales with file complexity)

**Expected steady-state**: ~1-2GB (oscillates during rebuilds, doesn't grow)

**Example project** (1300 files, 120 packages): Preamble ~1GB, steady-state ~1-2GB

## CRITICAL: Coroutine Lifecycle and Memory Leaks

**NEVER use `asio::detached` for lifecycle-bound tasks** (session creation, compilation, semantic indexing).

### The Problem

`asio::detached` keeps coroutine frames alive until io_context shutdown. Lambda captures (especially `shared_ptr<PreambleManager>`) are never released:

```cpp
// WRONG - causes unbounded memory growth
asio::co_spawn(executor,
    [preamble_manager]() -> asio::awaitable<void> {  // Captures shared_ptr
        // Heavy work...
        completion->complete.Set();  // Signals done, but frame PERSISTS
    },
    asio::detached);  // Frame never destroyed!
```

**Result**: Rapid saves accumulate old preambles (1GB + 1GB + 1GB...) → OOM crash.

### The Solution

Use `asio::use_awaitable` for lifecycle-bound tasks:

```cpp
auto task = asio::co_spawn(executor,
    [preamble_manager]() -> asio::awaitable<void> {
        // Heavy work...
    },
    asio::use_awaitable);  // Returns awaitable handle

active_tasks_.push_back(std::move(task));

// During preamble rebuild:
for (auto& task : active_tasks_) {
    co_await std::move(task);  // Destroys frame → releases shared_ptr
}
```

**Classification**:

- **Lifecycle-bound** (use `use_awaitable`): Session creation, compilation, AST building, indexing
- **Fire-and-forget** (can use `detached`): Logging, metrics, background cleanup with no resource captures

## Memory Profile

### PreambleManager (Shared, ~1GB)

Single Slang Compilation of all project files:

- **BumpAllocator**: AST nodes, types, symbols (bulk of memory)
- **SourceManager**: File buffers for all source files
- **Maps**: Package map, definition map

Shared by all OverlaySessions via `shared_ptr`.

### OverlaySession (Per-file, 5-100MB)

Per-file compilation referencing preamble:

- **Compilation**: Current file AST + PackageSymbol\* pointers → preamble (raw pointers)
- **SemanticIndex**: const Symbol\* pointers → preamble symbols (raw pointers)
- **shared_ptr<PreambleManager>**: Keeps preamble alive while raw pointers exist

**Critical invariant**: Session must hold `shared_ptr<PreambleManager>` to prevent dangling pointers.

## The Fragmentation Problem and mmap Solution

### Why malloc Causes Permanent Memory Growth

During preamble rebuild, old and new preambles allocate concurrently. malloc interleaves their allocations on the shared heap.

**Timeline of what happens**:

```
T0: Old preamble exists (1000 MB)
    Heap: [A][A][A][A][A][A][A][A]...

T1: User saves package file
    → Debounce timer starts (1500ms)

T2: Timer expires, new preamble build starts
    → Both preambles allocating concurrently
    Heap: [A][A][B][A][B][A][B][B]...  ← malloc interleaves!
    Peak: 2000 MB (both in memory)

T3: New preamble completes, swap happens
    → SessionManager.preamble_manager_ = new_preamble
    → Old preamble refcount drops (sessions cancelled)

T4: Old preamble destructor runs
    → BumpAllocator frees all segments: ::operator delete(seg)
    → Old segments freed to malloc
    Heap: [_][_][B][_][B][_][B][B]...  ← Mixed free/allocated!

T5: malloc tries to return memory to OS
    → Can only return WHOLE pages
    → Mixed pages (free + allocated) cannot be returned
    RSS: Still ~2000 MB (only ~95 MB freed)
```

**Root cause**: malloc allocates from a shared heap. When two preambles exist simultaneously, their segments interleave. After old preamble frees, its segments are scattered **between** new preamble's segments, preventing the allocator from returning pages to OS.

**Why debounce isn't enough**: Even with 1.5s delay between rebuilds, the old and new preamble coexist for 3-5 seconds during compilation. That's enough for malloc to interleave thousands of allocations, permanently fragmenting the heap.

### The mmap Fix

Changed Slang's BumpAllocator to use `mmap()` instead of `malloc()`:

**Before (malloc)**:

```cpp
BumpAllocator::Segment* allocSegment(Segment* prev, size_t size) {
    auto seg = (Segment*)::operator new(size);  // Allocates from shared heap
    seg->prev = prev;
    seg->current = (byte*)seg + sizeof(Segment);
    return seg;
}

BumpAllocator::~BumpAllocator() {
    Segment* seg = head;
    while (seg) {
        Segment* prev = seg->prev;
        ::operator delete(seg);  // Returns to heap, but pages still mixed
        seg = prev;
    }
}
```

**After (mmap)**:

```cpp
BumpAllocator::Segment* allocSegment(Segment* prev, size_t size) {
    auto seg = (Segment*)mmap(nullptr, size, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (seg == MAP_FAILED) throw std::bad_alloc();
    seg->prev = prev;
    seg->current = (byte*)seg + sizeof(Segment);
    seg->size = size;  // Store for munmap
    return seg;
}

BumpAllocator::~BumpAllocator() {
    Segment* seg = head;
    while (seg) {
        Segment* prev = seg->prev;
        size_t size = seg->size;
        munmap(seg, size);  // Immediately returns entire VA range to OS
        seg = prev;
    }
}
```

**Why this works**:

Virtual address isolation prevents interleaving:

```
Preamble 1: mmap() → VA 0x100000000-0x140000000 (1GB)
Preamble 2: mmap() → VA 0x200000000-0x240000000 (1GB)
            → Kernel allocates from separate VA ranges
            → No interleaving possible

Preamble 1 freed: munmap() → Kernel immediately reclaims entire 1GB range
                  → RSS drops by 1GB (not stuck at peak like malloc)
```

**Performance impact**: Negligible (~2μs per segment vs compilation taking seconds).

### Critical Requirements

Both fixes are necessary:

1. **mmap in BumpAllocator** (Slang fork) - Ensures memory CAN be returned
2. **Await coroutine tasks** (slangd) - Ensures destructor ACTUALLY runs (refcount → 0)

Without #2, old preambles never freed → mmap doesn't help → unbounded growth.

## Memory Management Strategy

### Preamble Lifetime (Refcounting)

```cpp
LanguageService.preamble_manager_       // refcount++
SessionManager.preamble_manager_        // refcount++
OverlaySession.preamble_manager_        // refcount++ per session
Lambda captures during build            // refcount++ per task
```

### Rebuild Sequence

```cpp
// 1. Wait for all active session tasks to complete
for (auto& task : active_session_tasks_) {
    co_await std::move(task);  // Destroys lambda → releases preamble shared_ptr
}

// 2. Clear sessions (drops refs to old preamble)
sessions_.clear();

// 3. Old preamble refcount → 0
//    → Destructor runs → munmap() → OS reclaims 1GB
```

### Design Principles

1. **Share preamble** across all sessions (not N copies)
2. **Cache sessions** for quick reopen (avoid 2s rebuild)
3. **Bound memory** via debounce (1.5s delay prevents overlap storms)
4. **Explicit cleanup** - await tasks, clear sessions, let refcount → 0

**Trade-off**: 2GB steady-state accepted (reasonable for full-featured LSP, simpler than alternatives).

## Monitoring

### Normal Behavior (With mmap)

```
Startup: ~500 MB
First build: ~1-1.5 GB
Rebuild peak: ~2-2.5 GB (old + new coexist)
After munmap: Drops to ~1-1.5 GB  ← RSS actually drops
Steady-state: 1-2 GB (oscillates, doesn't grow)
```

### What To Look For

**Expected log sequence during rebuild**:

```
[slangd][D] PreambleManager: Building from layout service
[slangd][D] Preamble build complete: 1658 MB -> 1641 MB (freed 17 MB)
[slangd][D] LanguageService rebuilt PreambleManager (121 packages, 699 definitions)
```

Shows new preamble built and old freed → RSS drops.

**Healthy pattern across multiple rebuilds**:

```
Rebuild 1: Peak 2.1 GB → Steady 1.2 GB  ← Memory returns
Rebuild 2: Peak 2.2 GB → Steady 1.3 GB  ← Still returns
Rebuild 3: Peak 2.1 GB → Steady 1.2 GB  ← Bounded oscillation
```

### Red Flags

**Unbounded growth** (coroutine leak):

```
Rebuild 1: Peak 2.1 GB → Steady 2.1 GB  ← Doesn't drop!
Rebuild 2: Peak 3.2 GB → Steady 3.2 GB  ← Growing!
Rebuild 3: Peak 4.3 GB → Steady 4.3 GB  ← Will OOM
```

**Signs to check**:

- Old preamble refcount > 2 (expected: LanguageService + SessionManager only)
- Missing destructor logs (indicates refcount never reaches 0)
- RSS never drops after "Preamble build complete" message

## Comparison to Other LSP Servers

| Server        | Typical RSS | Strategy                                   |
| ------------- | ----------- | ------------------------------------------ |
| clangd        | 1-3 GB      | Per-file compilation, no global index      |
| rust-analyzer | 1-3 GB      | Global index + per-file compilation        |
| pylsp         | 500 MB-1 GB | Python AST is smaller, less semantic info  |
| **slangd**    | **1-2 GB**  | **Shared preamble + per-file compilation** |

## Implemented Optimizations

**mmap-based BumpAllocator** (Slang fork):

- Replaced malloc/free with mmap/munmap
- Eliminates heap fragmentation via VA isolation
- Memory returns to OS when destructors run

**Coroutine task lifetime management** (slangd):

- Use `asio::use_awaitable` for lifecycle-bound tasks
- Store and await task awaitables (not just completion events)
- Ensures lambda captures released → refcount → 0 → destructor runs

## Future Optimizations

**Potential** (if memory becomes critical):

- Lazy preamble rebuild: Only rebuild when crossing files need packages
- Session eviction policy: More aggressive LRU for very large projects
- Preamble partitioning: Separate compilations per package group

Current architecture supports all LSP features at ~1-2GB RSS.
