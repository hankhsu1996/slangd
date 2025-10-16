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

## CRITICAL: Missing Symbols Cause Crashes

**FUNDAMENTAL CONSTRAINT**: Preamble and overlay use separate SourceManagers with independent BufferID spaces. Attempting to convert a preamble BufferID using overlay's SourceManager causes immediate crashes. The `symbol_info_` map enables safe conversion by providing pre-converted coordinates. Missing symbols require compilation identity checking to avoid crashes.

**Why crashes occur:**

1. Preamble visitor misses a symbol (e.g., struct field members not indexed)
2. Overlay references that symbol → `IsPreambleSymbol()` returns FALSE (not in map)
3. System assumes symbol is from overlay, tries to convert preamble BufferID with overlay SourceManager
4. **Result**: Invalid coordinates (line == -1) or SEGFAULT if BufferID maps to different file

**Proven case**: Struct field members (`s1.field_a`) are NOT in `symbol_info_` because visitor doesn't recurse into type definitions. Production crash confirmed.

**High-risk symbol types**: struct/union fields, enum members, class properties, nested types, interface modports.

**Required action when adding new symbol support**: Test with 100+ dummy packages to force high preamble BufferIDs, verify no crashes/invalid coordinates. See `test/slangd/semantic/preamble/package_preamble_test.cpp` for pattern.

### Crash Prevention Strategy

**Critical Discovery**: Cannot safely call SourceManager methods (getFileName, getLineNumber, etc.) with BufferIDs from a different SourceManager - they crash immediately. Validation must happen BEFORE attempting conversion.

**Solution**: Check symbol's compilation via pointer comparison:

```cpp
// In AddReference() - BEFORE calling ConvertSlangRangeToLspRange
const auto& symbol_compilation = symbol.getParentScope()->getCompilation();
const auto& preamble_compilation = preamble_manager->GetCompilation();

if (&symbol_compilation == &preamble_compilation) {
  // Symbol is from preamble BUT missing from symbol_info_
  // Skip reference - cannot safely convert (would crash)
  indexing_errors_.push_back(...);
  return;
}
```

**Why this works:**

- No SourceManager calls needed - pure pointer comparison
- Symbols know their compilation via `getParentScope()->getCompilation()`
- Cannot crash - no BufferID access, no array lookups
- 100% reliable - compilation identity is definitive

**Behavior:**

- Production: Gracefully skips invalid references, reports error via std::unexpected
- Tests: Fail immediately with diagnostic: "Symbol 'X' belongs to preamble compilation but is missing from symbol*info*"
- Developers: Clear action item - add symbol type to PreambleSymbolVisitor

**Additional mitigations:**

1. **Type member traversal**: PreambleSymbolVisitor recursively traverses struct/union/class members
2. **Test-driven coverage**: Comprehensive testing of each symbol type with BufferID offset patterns

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

### PreambleSymbolInfo Side Table

Overlay and preamble use separate SourceManagers with overlapping BufferIDs. Cannot use BufferID to distinguish symbol origins. Solution: symbol pointer identity plus precomputed locations.

Data structure:

```
struct PreambleSymbolInfo:
  file_uri: string              // Buffer-independent file path
  definition_range: LSP.Range   // Precise name range

symbol_info_: map<Symbol*, PreambleSymbolInfo>
```

When overlay resolves `import pkg::PARAM`, Slang returns the exact same pointer as preamble's PARAM symbol. Pointer identity enables O(1) lookup.

Location conversion:

```
function AddReference(symbol):
  if preamble_manager.IsPreambleSymbol(symbol):
    info = preamble_manager.GetSymbolInfo(symbol)
    AddCrossFileReference(..., info.definition_range, info.file_uri, ...)
  else:
    AddReference(...)  // Normal overlay symbol
```

### Package Storage

PreambleManager stores three key structures:

- `package_map_`: Maps package name to PackageSymbol\* for cross-compilation binding
- `symbol_info_`: Maps Symbol\* to PreambleSymbolInfo for LSP location lookup
- `preamble_compilation_`: Keeps preamble alive (Symbol\* pointers remain valid)

Visitor-based extraction:

```
class PreambleSymbolVisitor extends ASTVisitor:
  function handle(symbol):
    if not symbol.location.valid() or not symbol.getSyntax():
      return

    // Extract definition range using preamble's SourceManager
    file_uri = ConvertToUri(symbol.location, preamble_source_manager)
    range = definition_extractor.ExtractDefinitionRange(symbol, syntax)

    symbol_info_[&symbol] = {file_uri, ConvertToLspRange(range)}
    visitDefault(symbol)  // Recurse into nested symbols
```

## Design Rationale

### Eager Precomputation vs Lazy Extraction

**Approach chosen**: Build `symbol_info_` during `BuildFromLayout()` using ASTVisitor pattern.

**Alternative considered**: Lazy extraction - extract locations on first reference during overlay session.

Drawbacks of lazy approach:

- Requires mutex for thread-safe map updates (adds complexity)
- Still needs full traversal to know what exists (no fundamental performance benefit)
- Unpredictable latency - first reference to a symbol pays extraction cost
- Harder to test and reason about (race conditions possible)

Why eager extraction works better:

- Simple implementation - no threading complexity, read-only after build
- Predictable performance - consistent one-time cost during initialization
- Completeness guarantee - all symbols indexed before any session created
- Safe for concurrency - read-only access from all LSP operations
- Small cost - typical extraction takes 10-20ms for thousands of symbols

### Store All Package Symbols vs Filtering

**Approach chosen**: Visitor traverses ALL symbols recursively, stores all entries in `symbol_info_`.

**Alternative considered**: Filter by symbol kind - only store parameters, typedefs, functions, etc.

Drawbacks of filtering approach:

- Risk of missing symbols user might reference (variables, types, nested definitions)
- Maintenance burden - must update filter list when adding new symbol support
- No meaningful memory savings - side table overhead is negligible compared to package memory
- Potential correctness issues - hard to predict all symbol kinds users will reference

Why storing all symbols works better:

- Simple - one data structure, single source of truth
- Correct - if symbol in map it's from preamble, else it's from overlay (clean distinction)
- Future-proof - automatically supports new symbol kinds without code changes
- Small overhead - approximately 100 bytes per symbol (negligible vs package compilation memory)

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

**LSP features**: Single-point integration in `AddReference()` checks `IsPreambleSymbol()` before conversion. All LSP features (go-to-definition, hover, diagnostics) automatically support cross-compilation through this single check.

## Integration with Existing Architecture

See `SESSION_MANAGEMENT.md` for session lifecycle details. Package preamble integrates at:

- SessionManager: Passes `shared_ptr<PreambleManager>` to OverlaySession constructor
- OverlaySession: Stores preamble reference, uses PreambleAwareCompilation
- SemanticIndex: Queries `IsPreambleSymbol()` during reference creation

### Memory Impact

PreambleManager memory is shared across all sessions:

- Package compilations: O(package count × package size)
- Symbol side table: O(symbol count × per-symbol overhead)

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
