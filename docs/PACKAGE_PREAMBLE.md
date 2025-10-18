# Package Preamble Architecture

## Overview

Package preamble enables memory-efficient session caching by eliminating duplicate package compilation across OverlaySession instances. Instead of each session loading package files independently, all sessions share symbol references from a single preamble compilation.

Core mechanism: Cross-compilation symbol binding. Overlay sessions bind to PackageSymbol\* pointers from the preamble compilation, eliminating duplicate syntax tree loading and elaboration.

## Problem

SystemVerilog projects organize shared declarations (types, parameters, functions) into packages that are imported across many files. Without package sharing, each OverlaySession must:

1. Load all package files as SyntaxTrees
2. Parse and elaborate package contents
3. Store complete package ASTs in memory

Package memory consumption scales linearly with number of cached sessions, limiting cache capacity and forcing frequent recompilation.

## Solution: Cross-Compilation Symbol Binding

Slang symbols are location-independent pointers. A compilation can reference symbols from a separate compilation as long as the source compilation remains alive. This enables package sharing through pointer injection.

### Architecture

```
┌────────────────────────────────────────────┐
│ PreambleManager (Shared Compilation)       │
│  - Compiles all packages once              │
│  - Stores PackageSymbol* pointers          │
│  - Precomputes LSP locations (side table)  │
└────────────────────────────────────────────┘
                  ↓
┌────────────────────────────────────────────┐
│ OverlaySession (Per-File Compilation)      │
│  - Loads current file only                 │
│  - Injects preamble PackageSymbol*         │
│  - AST references cross-compilation        │
└────────────────────────────────────────────┘
                  ↓
┌────────────────────────────────────────────┐
│ LSP Features                               │
│  - Check preamble side table first         │
│  - Fall back to overlay SourceManager      │
└────────────────────────────────────────────┘
```

## Safe Conversion Architecture

**CRITICAL PRINCIPLE: Always derive SourceManager from the AST node that owns the range**

BufferIDs in source ranges belong to the SourceManager that parsed them. Using wrong SourceManager causes crashes.

### Slang Fork Enhancement

**Solution:** Added `Compilation* compilation` to `Expression` base class. Each expression stores its compilation.

**Impact:** Eliminates cross-compilation safety issues. Expressions automatically use correct SourceManager.

### Conversion Pattern

```cpp
// Symbols - derives SM from symbol's compilation
CreateLspLocation(symbol, range)

// Expressions - derives SM from expr.compilation
CreateLspLocation(expr, range)
```

**Default Argument Safety:** Calling `pkg::func()` with preamble default arguments now works correctly because the default expression stores preamble's compilation → uses preamble's SM.

**Obsolete:**
- ~~Preamble symbol checks~~ - Automatic now
- ~~Expression whitelist~~ - All safe
- ~~ConvertExpressionRange~~ - Removed

## Key Components

### PreambleAwareCompilation

Custom Compilation subclass that injects preamble packages before overlay syntax tree processing. The key constraint is that `getPackage()` is not virtual in Slang, preventing override. Solution: directly populate the protected `packageMap` member that `getPackage()` uses for lookups.

Implementation pattern:

```
class PreambleAwareCompilation extends Compilation:
  constructor(options, preamble_manager, current_file_path):
    for each package_info in preamble_manager.GetPackages():
      // Skip if package defined in current file (deduplication)
      if package_info.file_path == current_file_path:
        continue

      pkg = preamble_manager.GetPackage(package_info.name)
      packageMap[pkg.name] = pkg  // Direct injection
```

This requires a one-line Slang modification: `packageMap` visibility changed from `private` to `protected` in `Compilation.h`.

### Package Storage

PreambleManager stores two key structures:

- `package_map_`: Maps package name to PackageSymbol\* for cross-compilation binding
- `preamble_compilation_`: Keeps preamble alive (Symbol\* pointers remain valid)

No symbol side table needed. Location conversion uses `CreateSymbolLspLocation()` and `CreateLspLocation()` which automatically derive the correct SourceManager from each symbol's compilation.

## Design Rationale

### Direct packageMap Injection vs Method Override

**Constraint**: Method `getPackage()` is not virtual in Slang, preventing standard override pattern.

**Alternative considered**: Create wrapper/facade around Compilation that intercepts all package lookups.

Drawbacks of wrapper approach:

- Requires wrapping entire Compilation API surface
- Breaks direct Slang integration patterns
- High maintenance burden tracking Slang API changes
- Adds indirection layer with no functional benefit

Why direct injection works better:

- Minimal API surface - populate map in constructor, done
- Leverages existing Slang infrastructure (nameMap, WildcardImportData)
- One-line Slang modification (visibility change)
- No performance overhead

### Rebuild All Sessions on Preamble Change

When packages change (config changes, file modifications, file creation/deletion), Symbol\* pointers become invalid when preamble compilation is destroyed. All overlay references must be invalidated.

**Implementation**: SessionManager creates new PreambleManager, invalidates all OverlaySessions, sessions rebuilt on next access (lazy).

**Alternative considered**: Selective invalidation - track which sessions import which packages, only invalidate affected sessions.

Drawbacks of selective approach:

- Requires dependency tracking infrastructure (map sessions to imported packages)
- Package files can have transitive dependencies (one package imports another)
- Complexity in tracking indirect dependencies
- Edge cases with wildcard imports difficult to handle correctly
- Minimal benefit - config changes are rare in typical workflows

Why full rebuild works better:

- Simple implementation - no dependency tracking needed
- Correct by construction - impossible to miss a dependency
- Config changes are rare, so performance impact is acceptable
- Matches user expectations - configuration change affects entire project

### Return Complete PackageSymbol vs Individual Symbols

**Approach chosen**: `GetPackage(name)` returns complete `PackageSymbol*` pointer.

**Alternative considered**: Pre-extract individual symbols and return them on demand.

Why complete PackageSymbol works better:

- PackageSymbol already provides `Scope::nameMap` for O(1) lookups
- Includes `WildcardImportData` for `import pkg::*` caching
- Provides all Slang's existing lookup infrastructure
- No custom lookup implementation needed - Slang handles everything after pointer injection
- Matches Slang's architecture - minimal abstraction

## How It Works

**Package lookup**: Overlay calls `getPackage("my_pkg")` → finds preamble PackageSymbol\* in `packageMap` → Slang uses its nameMap → resolution works transparently.

**Wildcard imports**: `import pkg::*` requires no special handling. Slang's `WildcardImportData` caches resolutions using the injected PackageSymbol\* pointer.

**Type checking**: Types are symbols. When overlay references `pkg::my_type_t`, it gets the preamble TypeAliasSymbol\* with complete type information. Type checking operates normally on dereferenced pointers.

**AST traversal**: Overlay only traverses its own file's AST. Preamble symbols appear as leaf nodes (lookup results), never traversed, eliminating recursive package tree walks.

**LSP features**: All LSP features (go-to-definition, hover, diagnostics) automatically support cross-compilation. Location conversion functions derive the correct SourceManager from each symbol, handling preamble and overlay symbols uniformly without special checks.

## Integration with Existing Architecture

See `SESSION_MANAGEMENT.md` for session lifecycle details. Package preamble integrates at:

- SessionManager: Passes `shared_ptr<PreambleManager>` to OverlaySession constructor
- OverlaySession: Stores preamble reference, uses PreambleAwareCompilation
- SemanticIndex: Uses safe conversion functions that automatically handle preamble and overlay symbols

### Memory Impact

PreambleManager memory is shared across all sessions:

- Package compilations: O(package count × package size)

OverlaySession memory becomes file-scoped:

- Current file compilation only
- Interface files (if needed for port resolution)
- No package memory duplication

Lower per-session memory enables higher cache limit, fewer evictions, better responsiveness.

### Version Tracking

PreambleManager tracks version number, incremented on rebuild. SessionManager uses version to invalidate stale sessions that reference old preamble compilations.

### Deduplication Pattern

When opening a file containing package definitions, prevent duplicate package entries. The PreambleAwareCompilation constructor checks if a package's file path matches the current file being compiled. If so, it skips injecting that package, letting the overlay's package definition take precedence.

This enables editing package files without redefinition errors. Similar pattern used for interface deduplication in `overlay_session.cpp`.

## Testing Considerations

Design enables testing through:

- Pointer equality verification: Assert preamble and overlay get same Symbol\*
- Type checking validation: Verify diagnostics work across compilations
- Cross-file assertions: Test go-to-definition jumps to preamble file locations
- Deterministic behavior: No timing dependencies, no race conditions

Key test scenarios:

- Basic package import (`import pkg::PARAM`)
- Wildcard import (`import pkg::*`)
- Scoped references (`pkg::my_var`)
- Type resolution (`pkg::my_type_t`)
- Package file deduplication (opening package file itself)
- Scale testing (many packages, many symbols)

## Future Directions

Potential optimizations include incremental preamble updates (rebuild changed package only, invalidate sessions that import it), cross-file find references (index all package symbol references during preamble build), and persistent package index (serialize to disk, reload on restart).

## Related Documentation

- `SERVER_ARCHITECTURE.md`: Overall compilation architecture and two-compilation design
- `SESSION_MANAGEMENT.md`: Session lifecycle, caching, and memory bounds
- `SEMANTIC_INDEXING.md`: How SemanticIndex integrates with PreambleManager for cross-file references
