# Memory Architecture

## Overview

Slangd's memory usage is dominated by two components:

- **PreambleManager**: Shared compilation of all project files (scales with project size)
- **OverlaySessions**: Per-file compilation + semantic index (scales with file complexity)

**Expected steady-state**: Approximately 2× preamble size due to fragmentation during rebuilds (explained below)

**Example project** (1300 files, 120 packages): Preamble ~1GB, steady-state ~2GB

## Memory Profile Breakdown

### PreambleManager (Scales with Project Size)

Contains a single Slang Compilation of all project files (packages, interfaces, modules):

```
PreambleManager
├─ Compilation (bulk of memory)
│  ├─ BumpAllocator segments (AST nodes, types, symbols)
│  ├─ Package map
│  └─ Definition map
└─ SourceManager
   └─ File buffers for all source files
```

**Shared**: One preamble used by all OverlaySessions via `shared_ptr`.

**Typical sizes**:

- Small projects (<100 files): 100-300 MB
- Medium projects (500-1500 files): 500 MB - 1.5 GB
- Large projects (>3000 files): 2-5 GB

### OverlaySession (Scales with File Complexity)

Per-file compilation that references preamble:

```
OverlaySession
├─ Compilation (depends on file size and complexity)
│  ├─ Current file AST
│  └─ PackageSymbol* pointers → preamble (raw pointers, not ownership)
├─ SemanticIndex (depends on symbol count)
│  └─ const Symbol* pointers → preamble symbols (raw pointers)
└─ shared_ptr<PreambleManager> (keeps preamble alive)
```

**Typical sizes**:

- Simple files (<1000 lines, few imports): 5-15 MB
- Medium files (1000-3000 lines, moderate imports): 15-40 MB
- Complex files (>3000 lines, many imports, generate blocks): 40-100 MB

**Critical invariant**: Session must hold `shared_ptr<PreambleManager>` to keep raw pointers valid.

## The Pointer Dependency Chain

Sessions store **raw pointers** into preamble data structures:

```cpp
// In OverlaySession compilation:
compilation.packageMap["foo_pkg"] = preamble_pkg_symbol;  // raw pointer

// In SemanticIndex:
struct ReferenceEntry {
  const Symbol* target_symbol;  // raw pointer to preamble symbol
};
```

**Lifetime requirement**:

```
Session alive → Preamble alive (via shared_ptr)
Session freed → Preamble refcount--
All sessions freed → Preamble destructor runs
```

If preamble freed while session exists, raw pointers dangle and crash.

## The Fragmentation Problem

### What Happens During Preamble Rebuild

```
Timeline:

T0: Old preamble exists (1000 MB)
    Heap: [A][A][A][A][A][A][A][A]...

T1: User saves package file
    → Debounce timer starts (1500ms)

T2: Timer expires, new preamble build starts
    → Both preambles allocating concurrently
    Heap: [A][A][B][A][B][A][B][B]...
    Peak: 2000 MB (both in memory)

T3: New preamble completes, swap happens
    → SessionManager.preamble_manager_ = new_preamble
    → Old preamble refcount drops (sessions cancelled)

T4: Old preamble destructor runs
    → BumpAllocator frees all segments: ::operator delete(seg)
    → Old segments freed to mimalloc
    Heap: [_][_][B][_][B][_][B][B]...

T5: mimalloc tries to return memory to OS
    → Can only return WHOLE pages
    → Mixed pages (free + allocated) cannot be returned
    RSS: Still ~2000 MB (only ~95 MB freed)
```

**The root cause**: When two preambles allocate concurrently, malloc interleaves their allocations. After old preamble frees, its segments are **scattered between** new preamble's segments, preventing the allocator from returning pages to the OS.

### Why This Is Bounded

With debounce (1.5s between rebuilds):

- **Maximum overlap**: 2 preambles (old + new)
- **Peak RSS**: ~2GB
- **Steady-state**: ~2GB (fragmentation keeps it at peak)
- **No unbounded growth**: Next rebuild reuses freed segments

Without debounce (rapid saves):

- 10 overlapping preambles = 10GB peak
- 10GB becomes permanent floor due to fragmentation

## Why Changing Allocators Won't Help

### External Fragmentation Is Unavoidable

The fragmentation happens at the **heap level**, not the allocator level:

```
Layer 1: Application
  └─ PreambleManager objects (old + new coexist)

Layer 2: BumpAllocator (Slang internal)
  └─ Calls ::operator new() for 4KB segments
     └─ Sequential allocations: new(4KB), new(4KB), new(4KB)...

Layer 3: Heap (malloc/mimalloc/jemalloc)
  └─ Allocates from single shared heap
     └─ Thread 1 (old): [A][A][A]
         Thread 2 (new): [B][B][B]
         Result in heap: [A][B][A][B][A][B]  ← Interleaved!

Layer 4: OS (kernel pages)
  └─ Can only return WHOLE pages
     └─ Page with mix of [A] and [B] cannot be freed
```

**Why different allocators don't help**:

| Change                   | Why it doesn't help                            |
| ------------------------ | ---------------------------------------------- |
| mimalloc → jemalloc      | Interleaving still happens at heap level       |
| Different segment sizes  | Still interleaves, just different granularity  |
| One giant 1GB allocation | Entire preamble still coexists, same peak      |
| mmap() per segment       | Expensive syscalls (thousands of mmap), slower |

**The only solution**: Prevent temporal overlap (debounce achieves this).

### Could We Change BumpAllocator In Slang Fork?

**Yes, but not worth it:**

```cpp
// Option 1: mmap() per preamble (separate address space)
BumpAllocator::Segment* allocSegment(size_t size) {
  return (Segment*)mmap(nullptr, size, PROT_READ|PROT_WRITE,
                        MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
}
// Downside: Thousands of mmap() calls, kernel overhead, slower
```

```cpp
// Option 2: Pre-allocate huge chunk
BumpAllocator::BumpAllocator() {
  head = allocSegment(nullptr, 1GB);  // One allocation
}
// Downside: Still interleaves at heap level, wastes memory for small files
```

The complexity and performance cost don't justify minimal memory improvement.

## Memory Management Strategy

### Design Goals

1. **Share preamble** across all sessions (not N copies)
2. **Cache sessions** for quick reopen (avoid 2s rebuild)
3. **Bound memory** usage (prevent unbounded growth)
4. **Handle rapid saves** gracefully (debounce)

### Implementation

**Preamble lifetime**:

```cpp
// Shared ownership via refcounting
LanguageService.preamble_manager_       // refcount++
SessionManager.preamble_manager_        // refcount++
OverlaySession.preamble_manager_        // refcount++ per session
Lambda captures during build            // refcount++ per task
```

**Session caching**:

- LRU map with version tracking: `sessions_[uri] = {session, version}`
- Prefer evicting closed files over open files
- No size limit (preamble architecture makes sessions small)

**Preamble rebuild**:

- Debounced (1.5s delay) to prevent overlap storms
- Invalidates all sessions before swap
- Waits for active tasks to drain and release references
- Explicitly clears awaitable captures: `tasks_to_join.clear()`

### Trade-offs Accepted

**Memory for simplicity**:

- 2GB steady-state accepted (reasonable for full-featured LSP)
- Fragmentation bounded by preventing excessive overlap
- Simpler than alternatives (persistent index, process isolation)

**Eagerness for responsiveness**:

- Keep sessions cached (not evicted aggressively)
- Rebuild preamble on any package save (not lazy)
- Preemptively invalidate on config change

## Monitoring and Interpretation

### Reading Memory Logs

```
[slangd][D] Preamble swap: 2043 MB -> 2043 MB (freed 0 MB)
```

**Expected**: Before old preamble destructs, no memory freed yet.

```
[slangd][D] Cleared sessions: 2047 MB -> 2047 MB (freed 0 MB)
```

**Expected**: Sessions cleared, but old preamble still referenced by draining tasks.

```
[slangd][D] Preamble build complete: 1997 MB -> 1997 MB (freed 0 MB)
```

**Concerning if first build**: Should start at ~500 MB (base server).
**Expected if after rebuild**: Fragmentation keeps RSS at peak.

### When To Be Concerned

**Normal**:

- Steady-state: 1.5-2.5 GB (depends on project size)
- Peak during rebuild: 2-3 GB (overlapping preambles)
- RSS stays at peak after rebuild (fragmentation)

**Abnormal**:

- Unbounded growth: 2GB → 3GB → 4GB... (leak, not fragmentation)
- Extreme peak: >5 GB (too many overlapping rebuilds, debounce too short)
- No memory freed ever: Check refcount logs (something holding refs)

### Measuring True Leaks

Compare across multiple rebuild cycles:

```
Cycle 1: Build (1GB) → Rebuild (2GB) → Steady (2GB)
Cycle 2: Rebuild (2GB) → Steady (2GB)  ← Should stabilize
Cycle 3: Rebuild (2GB) → Steady (2GB)  ← Not 3GB, 4GB...
```

If RSS grows on every cycle (2GB → 3GB → 4GB), that's a leak. If it stabilizes at 2GB, that's fragmentation.

## Comparison to Other LSP Servers

| Server        | Typical RSS   | Strategy                                   |
| ------------- | ------------- | ------------------------------------------ |
| clangd        | 1-3 GB        | Per-file compilation, no global index      |
| rust-analyzer | 1-3 GB        | Global index + per-file compilation        |
| pylsp         | 500 MB - 1 GB | Python AST is smaller, less semantic info  |
| **slangd**    | **2 GB**      | **Shared preamble + per-file compilation** |

Slangd's 2GB is reasonable for SystemVerilog's complexity (packages, interfaces, hierarchical elaboration).

## Future Optimizations

**Not planned** (complexity not justified):

- Persistent global index: Complex incremental updates, serialization overhead
- Subprocess isolation: IPC overhead, process spawn cost
- Custom BumpAllocator: Minimal benefit, Slang fork maintenance burden

**Potential** (if memory becomes critical):

- Lazy preamble rebuild: Only rebuild when crossing files need packages
- Session eviction policy: More aggressive LRU for very large projects
- Preamble partitioning: Separate compilations per package group

Current architecture supports all LSP features (hover, completion, references) without requiring changes.
