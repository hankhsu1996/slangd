# Preamble Architecture

## Overview

Preamble enables memory-efficient session caching by eliminating duplicate compilation of shared definitions (packages, interfaces, modules) across OverlaySession instances. Instead of each session loading these files independently, all sessions share symbol references from a single preamble compilation.

Core mechanism: Cross-compilation symbol binding. Overlay sessions bind to Symbol\* pointers from the preamble compilation, eliminating duplicate syntax tree loading and elaboration.

## Problem

SystemVerilog projects organize shared declarations into three types of constructs:

1. **Packages**: Shared types, parameters, functions imported via `import pkg::*`
2. **Interfaces**: Port bundles instantiated across module hierarchies
3. **Modules**: Reusable logic blocks instantiated in design hierarchies

Without preamble sharing, each OverlaySession must:

1. Load all shared definition files as SyntaxTrees
2. Parse and elaborate their contents
3. Store complete ASTs in memory

Memory consumption scales linearly with number of cached sessions, limiting cache capacity and forcing frequent recompilation.

## Solution: Cross-Compilation Symbol Binding

Slang symbols are location-independent pointers. A compilation can reference symbols from a separate compilation as long as the source compilation remains alive. This enables sharing through pointer injection.

### Architecture

```
┌────────────────────────────────────────────┐
│ PreambleManager (Shared Compilation)       │
│  - Compiles all shared files once          │
│  - Stores Symbol* pointers                 │
│    * PackageSymbol (for packages)          │
│    * DefinitionSymbol (for modules/ifaces) │
│  - NO preprocessing or metadata extraction │
└────────────────────────────────────────────┘
                  ↓
┌────────────────────────────────────────────┐
│ OverlaySession (Per-File Compilation)      │
│  - Loads current file only                 │
│  - Injects preamble Symbol* pointers       │
│    * packageMap (packages)                 │
│    * definitionMap (modules/interfaces)    │
│  - AST references cross-compilation        │
└────────────────────────────────────────────┘
                  ↓
┌────────────────────────────────────────────┐
│ LSP Features                               │
│  - CreateLspLocation() auto-derives SM     │
│  - No manual preamble checks needed        │
└────────────────────────────────────────────┘
```

## Thread-Safety Constraint

**Architectural consequence**: Preamble's 1:many sharing inherently creates concurrent access to shared symbols. When overlay sessions run concurrently on the thread pool (for performance and responsiveness), multiple threads simultaneously access the same preamble compilation.

**Slang's design assumption**: Single-threaded sequential access. Many Slang operations are lazy (deferred until first access) and assume no concurrent modification. Examples include scope elaboration, name resolution caching, type construction, and symbol binding.

**The gap**: Preamble architecture creates multi-threaded access pattern that Slang was not designed for. This is not a feature requirement - it's an emergent property of the architecture:

```
Preamble sharing (1:many) + Concurrent overlay compilation = Concurrent preamble access
```

**Consequence**: Any lazy operation in Slang that modifies state can race when multiple overlay sessions trigger it simultaneously on shared preamble symbols.

**Solution**: Serialize overlay elaboration using asio strand on multi-threaded compilation pool. Preserves parallel preamble parsing while preventing concurrent preamble access. No Slang modifications required.

## Safe Conversion Architecture

**CRITICAL PRINCIPLE: Always derive SourceManager from the AST node that owns the range**

BufferIDs in source ranges belong to the SourceManager that parsed them. Using wrong SourceManager causes crashes.

**Important**: BufferIDs are per-compilation indices, not global identifiers. Preamble BufferID 15 and overlay BufferID 15 are completely different buffers. You cannot use `buffer.getId() < bufferCount` to detect cross-compilation - the IDs overlap.

### Slang Fork Enhancements

**Solutions:**

1. Added `Compilation* compilation` to `Expression` base class (commit 42ed17f3)
2. Added `Compilation* compilation` to `Symbol` base class (current work)

Each expression and symbol stores its compilation, enabling automatic SourceManager derivation.

**Impact:** Eliminates cross-compilation safety issues. All AST nodes automatically use correct SourceManager.

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

Custom Compilation subclass that injects preamble symbols before overlay syntax tree processing. Implementation is straightforward: populate protected maps that Slang uses for lookups.

**Injection points:**

1. **Packages**: Direct `packageMap` population (requires `packageMap` visibility: `private` → `protected` in Slang)
2. **Modules/Interfaces**: Direct `definitionMap` population (already protected in Slang)

**Deduplication pattern:**

```cpp
class PreambleAwareCompilation extends Compilation:
  constructor(options, preamble_manager, current_file_path):
    // Inject packages
    for each package in preamble_manager.GetPackages():
      if package.file_path != current_file_path:  // Skip if defined in current file
        packageMap[package.name] = package.symbol

    // Inject definitions (modules/interfaces)
    for each definition in preamble_manager.GetDefinitions():
      if definition.file_path != current_file_path:  // Skip if defined in current file
        definitionMap[definition.name] = definition.symbol
```

This prevents redefinition errors when editing files that contain package/module/interface definitions.

### PreambleManager Storage

PreambleManager is remarkably simple - just symbol pointer storage:

```cpp
class PreambleManager {
  std::unordered_map<std::string, const PackageSymbol*> packages_;
  std::unordered_map<std::string, DefinitionEntry> definitions_;  // modules + interfaces
  std::unique_ptr<Compilation> preamble_compilation_;  // Keeps symbols alive
};
```

**No preprocessing, no metadata extraction, no side tables.** Symbol injection is sufficient - Slang handles all lookup logic.

Location conversion uses `CreateSymbolLspLocation()` and `CreateLspLocation()` which automatically derive the correct SourceManager from each symbol's compilation.

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

### Return Complete Symbol Pointers vs Individual Members

**Approach chosen**: Return complete symbol pointers (`PackageSymbol*`, `DefinitionSymbol*`).

**Alternative considered**: Pre-extract individual members and return them on demand.

Why complete symbols work better:

- Symbols already provide `Scope::nameMap` for O(1) lookups (packages)
- `DefinitionSymbol` already provides `parameters` and `portList` (modules/interfaces)
- Includes all Slang's existing lookup infrastructure
- No custom lookup implementation needed - Slang handles everything after pointer injection
- Matches Slang's architecture - minimal abstraction

## How It Works

**Package imports**: Overlay calls `getPackage("my_pkg")` → finds preamble PackageSymbol\* in `packageMap` → Slang uses its nameMap → resolution works transparently. Wildcard imports (`import pkg::*`) work automatically via Slang's `WildcardImportData`.

**Module/interface instantiation**: Overlay calls `getDefinition("my_module")` → finds preamble DefinitionSymbol\* in `definitionMap` → Slang creates instance using definition metadata → instantiation works transparently.

**Cross-file navigation**: When user clicks on a port/parameter name in an instantiation:

1. SemanticIndex finds the `UninstantiatedDefSymbol` for that instance
2. Calls `symbol.getDefinition()` to get the preamble DefinitionSymbol\*
3. Accesses `definition->portList` or `definition->parameters` to find the declaration
4. Uses `CreateLspLocation()` which auto-derives the preamble SourceManager
5. Returns LSP location pointing to definition file

**Type checking**: Types are symbols. When overlay references `pkg::my_type_t` or instantiates `my_module`, it gets preamble symbol pointers with complete type/definition information. Type checking operates normally on dereferenced pointers.

**AST traversal**: Overlay only traverses its own file's AST. Preamble symbols appear as leaf nodes (lookup results), never traversed, eliminating recursive tree walks.

**LSP features**: All LSP features automatically support cross-compilation. Location conversion functions derive the correct SourceManager from each symbol's compilation, handling preamble and overlay symbols uniformly without special checks.

## Integration with Existing Architecture

See `SESSION_MANAGEMENT.md` for session lifecycle details. Preamble integrates at:

- SessionManager: Passes `shared_ptr<PreambleManager>` to OverlaySession constructor
- OverlaySession: Stores preamble reference, uses PreambleAwareCompilation
- SemanticIndex: Uses safe conversion functions that automatically handle preamble and overlay symbols

### Memory Impact

PreambleManager memory is shared across all sessions:

- Preamble compilation: O(packages + interfaces + modules)
- Single compilation regardless of session count

OverlaySession memory becomes file-scoped:

- Current file compilation only
- No duplication of packages, interfaces, or modules
- Dramatically reduced per-session footprint

Lower per-session memory enables higher cache limit, fewer evictions, better responsiveness.

### Version Tracking

PreambleManager tracks version number, incremented on rebuild. SessionManager uses version to invalidate stale sessions that reference old preamble compilations.

### Deduplication Pattern

When opening a file containing definitions (packages, interfaces, modules), prevent duplicate entries. The PreambleAwareCompilation constructor checks if a definition's file path matches the current file being compiled. If so, it skips injecting that symbol, letting the overlay's definition take precedence.

This enables editing definition files without redefinition errors. Applied uniformly to packages, interfaces, and modules in `overlay_session.cpp`.

## Testing Considerations

Design enables testing through:

- Pointer equality verification: Assert preamble and overlay get same Symbol\*
- Type checking validation: Verify diagnostics work across compilations
- Cross-file assertions: Test go-to-definition jumps to preamble file locations
- Deterministic behavior: No timing dependencies, no race conditions

Key test scenarios:

**Packages:**

- Basic import (`import pkg::PARAM`)
- Wildcard import (`import pkg::*`)
- Scoped references (`pkg::my_var`)
- Type resolution (`pkg::my_type_t`)

**Interfaces:**

- Interface instantiation (`my_iface bus()`)
- Signal access through interface (`bus.data`)

**Modules:**

- Module instantiation with ports (`my_module inst(.a(x))`)
- Module instantiation with parameters (`my_module #(.WIDTH(8)) inst`)
- Cross-file port/parameter navigation

**Deduplication:**

- Opening files that contain definitions (no redefinition errors)

**Scale:**

- Many packages/interfaces/modules
- Many symbols per construct

## Future Directions

Potential optimizations include:

- Incremental preamble updates (rebuild changed files only, invalidate dependent sessions)
- Cross-file find references (index all symbol references during preamble build)
- Persistent index (serialize to disk, reload on restart)
- Additional constructs (programs, checkers) following same pattern

## Related Documentation

- `SERVER_ARCHITECTURE.md`: Overall compilation architecture and two-compilation design
- `SESSION_MANAGEMENT.md`: Session lifecycle, caching, and memory bounds
- `SEMANTIC_INDEXING.md`: How SemanticIndex integrates with PreambleManager for cross-file references
