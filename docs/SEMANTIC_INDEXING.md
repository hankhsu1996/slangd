# Semantic Indexing Architecture

## Overview

SemanticIndex enables LSP features (go-to-definition, find references, document symbols) through unified symbol storage and reference tracking in a single AST pass.

**Key Components:**

- **SemanticIndex**: Stores symbols and references
- **IndexVisitor**: AST visitor that processes symbols and expressions
- **DefinitionExtractor**: Extracts precise name ranges from syntax nodes
- **References Vector**: Stores source range → target definition mappings

**How It Works:**

1. For each definition: create instance via `createDefault()` → call `forceElaborate()` → populate `diagMap` + cache symbol resolutions
2. Visit instance bodies: `ProcessSymbol()` creates entries, `handle()` methods create references
3. `LookupDefinitionAt()` searches `references_` for cursor position, returns target definition

**Note:** `forceElaborate()` is file-scoped (not full design elaboration) and enables both semantic diagnostics and faster indexing through cached resolutions.

## DocumentSymbol Architecture

**Separation of Concerns:**

- **SemanticIndex** performs all semantic analysis (symbol resolution, scope traversal, specialization)
- **DocumentSymbolBuilder** performs pure data transformation (SemanticEntry[] → DocumentSymbol[])

**Critical Rule:** DocumentSymbolBuilder must never call Slang semantic APIs. All computed information must be stored in SemanticEntry during indexing.

### Non-Scope Symbols with Children

Some symbols are not Scopes but have children in a related Scope (e.g., GenericClassDefSymbol has members in its ClassType specialization).

**Solution:** Store children lookup scope in SemanticEntry:

```cpp
struct SemanticEntry {
  const Scope* parent;           // Where this symbol belongs in tree
  const Scope* children_scope;   // Where to find children (for non-Scope symbols)
};
```

**Pattern:**

- Semantic work (e.g., `getDefaultSpecialization()`) done once in handler
- Store resulting scope pointer in `children_scope` field
- DocumentSymbolBuilder uses stored pointer for children lookup

**Example:** GenericClassDefSymbol stores ClassType scope from `getDefaultSpecialization()` to avoid re-computation in DocumentSymbolBuilder.

## Slang Symbol Organization (Library Constraints)

Critical knowledge about Slang's architecture that impacts LSP implementation.

### DefinitionSymbol Has No Body

**Constraint:** `DefinitionSymbol` is NOT a `Scope` - it's only a template/declaration.

- Cannot call `.members()` on DefinitionSymbol
- Body members only exist in `InstanceBodySymbol` (after elaboration)
- Must call `compilation.getRoot()` to trigger elaboration

### Symbol Namespaces are Disjoint

Slang organizes symbols into separate collections with no overlap:

- `getDefinitions()` - Module/interface/program headers
- `getPackages()` - Package scopes
- `getCompilationUnits()` - $unit scope (classes/enums/typedefs)
- `getRoot().members()` - Elaborated instances

This means no deduplication needed when traversing all collections.

### LSP Mode Elaboration

Calling `getRoot()` in `LanguageServerMode`:

- Treats each file as standalone (no top-module requirement)
- Auto-instantiates modules/programs as root members
- Auto-instantiates interfaces during port resolution (nested in modules)

This is how definition bodies become accessible for indexing.

### Critical: Proper Instance Creation for Body Traversal

**Requirement**: When indexing definition bodies, create full `InstanceSymbol` objects via `createDefault()`, not standalone `InstanceBodySymbol` via `fromDefinition()`.

**Technical Reason**:

`InstanceBodySymbol::fromDefinition()` creates a body with `parentInstance = nullptr`. During visitor traversal, when expressions are evaluated (required for LSP to index all symbol references), Slang performs lazy elaboration. For interface ports, this triggers `InterfacePortSymbol::getConnectionAndExpr()` which requires a valid `parentInstance` pointer to resolve port connections.

**Consequences of Incorrect Usage**:

- Segmentation fault in `InterfacePortSymbol::getConnectionAndExpr()` at PortSymbols.cpp:1676
- Crash occurs during expression evaluation, not during initial symbol creation
- Affects any module/interface with interface ports or expressions referencing interface members

**Correct Implementation**:

1. Use `InstanceSymbol::createDefault()` to create instances with proper parent linkage
2. Call `instance.setParent(*definition.getParentScope())` to establish scope hierarchy
3. Traverse `instance.body` (not a standalone body)
4. In LSP mode, `connectDefaultIfacePorts()` automatically creates default interface instances for port resolution

**When This Applies**:

- All module/interface definition body traversal
- Any code path that visits expressions containing interface port references
- Generic traversal infrastructure that must handle all SystemVerilog constructs

### Preventing Duplicate Interface Traversal

**Problem**: Interface instances appear twice during indexing - once as standalone (when processing interface definition) and once nested in modules (Slang creates full instances for port resolution).

**Why interfaces differ from sub-modules**:

- Sub-modules use `UninstantiatedDefSymbol` in LSP mode → visitor doesn't traverse them
- Interfaces use full `InstanceSymbol` → visitor traverses them by default → causes duplicates

**Solution**: Add `handle(InstanceSymbol&)` that checks parent scope:

- Parent = `CompilationUnit` → standalone interface definition → traverse body
- Parent ≠ `CompilationUnit` → nested in module → skip body (already indexed)

**Implementation**: `semantic_index.cpp:1549-1570`

## Adding New Symbol Support

### 1. Definition Extraction

Add to `DefinitionExtractor::ExtractDefinitionRange()`:

```cpp
case SK::MySymbol:
  if (syntax.kind == SyntaxKind::MyDeclaration) {
    return syntax.as<MyDeclarationSyntax>().name.range();
  }
  break;
```

**Key Points:**

- Extract precise name token range, not full declaration
- Check Slang's `AllSyntax.h` for syntax types
- Use `slang --ast-json` to verify actual syntax kinds used
- Handle null checks for optional syntax elements

**Common Pitfall:** Methods like `setFromDeclarator()` can change syntax references after symbol creation. For example, variables created from `DataDeclarationSyntax` return `DeclaratorSyntax` from `getSyntax()`.

### 2. Self-Definition Handler

Use `AddDefinition()` to create symbol definitions. For end labels (e.g., `endmodule : Test`), use `AddReference()` with `syntax->endBlockName->name.range()`.

### 3. Reference Handler

Add handlers for expressions that reference your symbol:

```cpp
void IndexVisitor::handle(const MyExpression& expr) {
  const auto* target = expr.getTargetSymbol();
  CreateReference(expr.sourceRange, *target);
  this->visitDefault(expr);
}
```

**Common Expression Types:** `NamedValueExpression`, `CallExpression`, `ConversionExpression`

### 4. Include Headers & Test

Add includes to `semantic_index.cpp` and create tests in `definition_test.cpp`:

```cpp
TEST_CASE("my_symbol self-definition") {
  fixture.AssertGoToDefinition(*index, code, "my_symbol", 0, 0);
}
```

Test parameters: `reference_index` (which occurrence to click), `definition_index` (target occurrence)

## Safe Range Conversion (Critical for Cross-Compilation)

**SAFETY-FIRST PRINCIPLE: Always match SourceManager to BufferID ownership**

With cross-compilation (PreambleManager + OverlaySession), using the wrong SourceManager causes crashes or incorrect line numbers. BufferIDs in source ranges belong to the SourceManager that parsed the syntax.

### Decision Tree for Range Conversion

**Question 1: Is this a definition or reference?**

**If DEFINITION:**

```cpp
// Use symbol.location (points to definition name)
auto def_loc = CreateSymbolLspLocation(symbol);
```

**If REFERENCE, ask Question 2:**

**Question 2: Where does the syntax come from?**

**Path A - Symbol's Own Syntax** (from `symbol.getSyntax()`):

```cpp
// Example: End block name reference
const auto* syntax = subroutine.getSyntax();
const auto& func_syntax = syntax->as<FunctionDeclarationSyntax>();
auto ref_loc = CreateLspLocation(subroutine, func_syntax.endBlockName->name.range());
```

Use `CreateLspLocation(symbol, range)` - derives SM from symbol's compilation.

**When:** Range extracted from `symbol.getSyntax()` syntax tree.

**Path B - Current File Expression** (from expression/syntax visitor):

```cpp
// Example: Expression referencing preamble symbol
auto reference_range = expr.sourceRange;  // From overlay
auto ref_loc = ConvertExpressionRange(reference_range);
```

Use `ConvertExpressionRange(range)` - uses overlay's SM (IndexVisitor's compilation).

**When:** Range from current file expressions, import statements, parameter syntax, etc.

### Why This Matters

**Critical scenario:** Referencing preamble symbol from overlay syntax.

```cpp
// Current file (overlay): uses pkg::BUS_WIDTH
// BUS_WIDTH symbol: from preamble compilation
// identifier "BUS_WIDTH": parsed by overlay's SourceManager

// WRONG - causes line number mismatch:
auto ref_loc = CreateLspLocation(pkg_symbol, expr.identifier.range());
// Uses preamble's SM with overlay's BufferID → wrong file mapping

// CORRECT - uses matching SM:
auto ref_loc = ConvertExpressionRange(expr.identifier.range());
// Uses overlay's SM with overlay's BufferID → correct mapping
```

### Common Expression Path Scenarios

All these use syntax from **current file** and require `ConvertExpressionRange`:

```cpp
// Import statements
auto ref_loc = ConvertExpressionRange(import_item.package.range());
auto ref_loc = ConvertExpressionRange(import_item.item.range());

// Named parameters (instance syntax)
auto ref_loc = ConvertExpressionRange(named_param.name.range());

// Package scoped identifiers (pkg::item)
auto ref_loc = ConvertExpressionRange(ident.identifier.range());

// Value expressions
auto ref_loc = ConvertExpressionRange(expr.sourceRange);
```

### Implementation Notes

**ConvertExpressionRange:** "Pragmatic exception" - expressions don't provide SM derivation, so IndexVisitor uses its compilation's SM (overlay). This is safe because all expression syntax comes from the current file being indexed.

**CreateLspLocation(symbol, range):** Derives SM via `symbol.getParentScope()->getCompilation()`. Use only when range comes from symbol's own syntax tree.

**Rule of thumb:** If you're in an expression handler (`handle(SomeExpression&)`) or processing syntax from import/parameter/instantiation statements, use `ConvertExpressionRange`. If you're processing a symbol's own syntax node retrieved via `symbol.getSyntax()`, use `CreateLspLocation(symbol, range)`.

### Expression Whitelist (Critical Safety Constraint)

**CRITICAL:** `ConvertExpressionRange` is **ONLY** safe for **whitelisted expression handlers**.

After several segfaults from BufferID/SourceManager mismatches, we adopted a safety-first whitelist approach:

**Whitelisted:** Expression handlers (`handle(NamedValueExpression&)`, `handle(CallExpression&)`), import statements, instantiation parameters.

**NOT whitelisted:** TypeReference handlers, symbol-derived syntax (`symbol.getSyntax()`), end block names.

**Pattern:** Functions accepting syntax from multiple contexts use `std::optional<Symbol&> syntax_owner` to explicitly select conversion path. `has_value()` → Symbol Path, `nullopt` → Expression Path (whitelisted only).

See `PACKAGE_PREAMBLE.md` for detailed safety architecture.

## Cross-File Module Instantiation

Module navigation differs from other features due to cross-compilation metadata lookup.

### Architecture

**Two-Compilation Design:**

- **PreambleManager**: Compiles all files once, extracts module metadata (ModuleInfo with ports/parameters)
- **OverlaySession**: Compiles current file + packages + interfaces only
- **SemanticIndex**: Indexes local symbols, queries PreambleManager for cross-file metadata

### UninstantiatedDefSymbol Pattern

When a module is instantiated but its definition is not in the current compilation, Slang creates `UninstantiatedDefSymbol` instead of `InstanceSymbol`.

**LSP Mode Modification:** In LSP mode, all non-interface module/program instantiations are forced to use `UninstantiatedDefSymbol` for consistency (see `InstanceSymbols.cpp`). Exception: Interfaces require full elaboration for member access.

**Handler Features:**

1. Instance name self-definition (via `AddDefinition`)
2. Parameter expression navigation (visit `symbol.paramExpressions`)
3. Port connection navigation (visit `symbol.getPortConnections()`)
4. Cross-file module type navigation (query `preamble_manager_->GetModule()`)

**Pattern:** Following generate loop expression preservation, visit expressions stored in `UninstantiatedDefSymbol` directly without manual syntax parsing.

**Integration:**

- Add `CompilationFlags::IgnoreUnknownModules` to suppress unknown module diagnostics
- SemanticIndex receives optional `PreambleManager*` parameter (nullptr for single-file mode)
- O(1) module lookup via hash map, O(n) port/parameter lookup (~20 entries)

**Cross-File Reference Resolution:**

Module definitions exist in PreambleManager's compilation (different `SourceManager`), not OverlaySession's. Use `AddReferenceWithLspDefinition()` with pre-converted LSP coordinates from PreambleManager. This enables go-to-definition across compilations using compilation-independent coordinates (file URI + line/column).

## Slang Fork Modifications

### 1. Compiler-Generated Variable Redirection

**Problem:** Slang creates temporary variables for constructs like genvar loops. LSP should redirect to user-visible symbols.

**Solution:** Add `getDeclaredSymbol()/setDeclaredSymbol()` to `VariableSymbol` (following `ModportPortSymbol::internalSymbol` pattern). Store genvar pointer during construction, redirect in LSP:

```cpp
if (variable.flags.has(VariableFlags::CompilerGenerated)) {
  if (const auto* declared = variable.getDeclaredSymbol()) {
    target_symbol = declared;
  }
}
```

### 2. Expression Preservation Pattern

**Problem:** Compiler evaluates expressions to constants and discards the AST, breaking LSP navigation.

**Solution**: Store bound expressions alongside constants in Slang. Structure mirrors the constant (single/pair/variant).

**Process**:

1. Find where Slang evaluates the expression (`evalInteger()` or `eval()`)
2. Identify the struct storing the constant result
3. Add expression field(s) matching the constant's structure
4. Store bound expression before evaluation
5. Visit stored expressions in slangd's semantic indexer

**Examples**: Array dimensions (`EvaluatedDimension`, commit 5c5eee26), hierarchical selectors (`HierarchicalReference::Element`, commit 1d1543bb).

**Key insight**: Compiler has bound expression at evaluation time - just save it. BumpAllocator makes storage cheap.

## Design Principles

### No Overlapping Ranges

**Critical Rule:** Source ranges in `references_` should NEVER overlap. If they do, fix the root cause in reference creation.

**Wrong:** Add disambiguation logic to `LookupDefinitionAt()`
**Correct:** Use precise name token ranges, not full syntax ranges

### Handler Patterns

- **Symbol handlers:** Process definitions, create self-references
- **Expression handlers:** Process usage, create cross-references
- **Always call** `visitDefault()` to continue traversal

### Definition Range Extraction

- Prefer precise name token ranges
- Handle optional syntax elements gracefully
- Fallback to `syntax.sourceRange()` when precise extraction impossible

## Debugging

**AST Investigation:**

```bash
mkdir -p debug
echo 'test code' > debug/test.sv
slang debug/test.sv --ast-json debug/ast.json
slang debug/test.sv --cst-json debug/cst.json
```

**Temporary Logging:**

Use `spdlog::debug()` for temporary logging, remove before committing.

**Finding Handlers:**

Indexing logic can span multiple handlers via `visitDefault()` calls. To locate handlers for an expression type:

- Search for the expression class name in `semantic_index.cpp`
- Add `spdlog::debug()` at handler entry to trace execution

**Common Issues:**

- "LookupDefinitionAt failed": Missing expression handler or unhandled syntax kind
- "No symbol found": Missing definition extraction
- "Wrong definition target": Check reference creation logic
- Overlapping ranges: Fix reference creation, don't add disambiguation

**Test Development:** Start with failing tests, add logging, implement minimal fix, clean up.

## Type Reference Handling

See `LSP_TYPE_HANDLING.md` for typedef and type reference details.

## Examples

See subroutine implementation:

- Definition extraction: `SK::Subroutine` in `definition_extractor.cpp`
- Self-references: `handle(SubroutineSymbol&)` in `semantic_index.cpp`
- Call references: `handle(CallExpression&)` in `semantic_index.cpp`
- Tests: `definition_test.cpp`
