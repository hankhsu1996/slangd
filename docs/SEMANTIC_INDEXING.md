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

## Document Symbols (Syntax-Based)

**Current Implementation:**

- **Production**: Uses `SyntaxDocumentSymbolVisitor` for fast syntax-based document symbols
- **Testing**: SemanticIndex still used in tests to verify semantic analysis correctness

**Key Trade-off:**

- Syntax-based approach has no session/preamble dependency, responds immediately
- Cannot distinguish interface ports from regular ports (requires semantic analysis)
- Accepted limitation per PLAN.md - prioritizes speed and robustness over perfect classification

### Non-Scope Symbols with Children (Semantic Context)

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
- Avoids re-computation when traversing children

**Example:** GenericClassDefSymbol stores ClassType scope from `getDefaultSpecialization()` to avoid re-computation.

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

### Dangerous APIs That Trigger Elaboration

**CRITICAL:** The following Compilation APIs trigger full elaboration via `forceElaborate()`:

- `compilation.getRoot()` - Auto-instantiates all top-level modules/programs
- `compilation.getAllDiagnostics()` - Calls getRoot() internally to collect all diagnostics
- `compilation.getSemanticDiagnostics()` - Calls getRoot() internally

**Why This Matters:**

`forceElaborate()` iterates `definitionMap` and creates Instances from definitions. With preamble injection, definitionMap contains multiple definitions per name (different library priorities). Without proper handling, this creates duplicate Instances and redefinition errors.

**Safe Alternatives:**

- Use `compilation.getCollectedDiagnostics()` - Reads diagMap without triggering elaboration
- SemanticIndex manually calls `forceElaborate(instance.body)` for controlled elaboration
- Production code should NEVER call getRoot(), getAllDiagnostics(), or getSemanticDiagnostics()

**LSP Mode Elaboration (When Triggered):**

Calling `getRoot()` in `LanguageServerMode`:

- Treats each file as standalone (no top-module requirement)
- Auto-instantiates modules/programs as root members
- Auto-instantiates interfaces during port resolution (nested in modules)
- Our Slang fork: only processes highest-priority definition per name (prevents preamble duplicates)

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

**Why interfaces need special handling**:

In LSP mode with `SkipBody` flag optimization:

- Sub-module instances: Body members skipped → visitor doesn't traverse nested body → no duplicates
- Interface instances: Body NOT skipped (always elaborate) → visitor traverses nested instances → causes duplicates

Interfaces require full elaboration even when nested because interface port connections need member access.

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

**SAFETY-FIRST PRINCIPLE: Always derive SourceManager from the AST node that owns the range**

With cross-compilation (PreambleManager + OverlaySession), using the wrong SourceManager causes crashes or incorrect line numbers. BufferIDs in source ranges belong to the SourceManager that parsed the syntax.

### Slang Fork Enhancement: Expression.compilation Pointer

**Solution:** Modified Slang library to add `Compilation* compilation` to `Expression` base class. Each expression stores the compilation that created it.

**Impact:** Eliminates cross-compilation safety concerns. Expressions automatically use correct SourceManager (preamble expressions → preamble SM, overlay expressions → overlay SM).

### Universal Conversion Pattern

**Rule:** Always use `CreateLspLocation(node, range)` or `CreateSymbolLspLocation(symbol, range)` to derive SourceManager from the AST node. Never use SourceManager directly.

```cpp
// Symbols - derives SM from symbol's compilation
CreateSymbolLspLocation(symbol, range)
CreateLspLocation(symbol, range)

// Expressions - derives SM from expr.compilation
CreateLspLocation(expr, range)
```

**Important:** `CreateSymbolLspLocationWithSM` is a low-level helper function used internally by `CreateLspLocation` and `CreateSymbolLspLocation`. It should only be called directly in very rare cases where you already have the correct SourceManager. In almost all cases, use the higher-level functions that automatically derive the SourceManager from the AST node.

**Key Benefit:** Handles default argument expressions correctly. When calling `pkg::func()` with preamble default arguments, the default arg expression stores preamble's compilation → automatic correct SM selection.

**Obsolete Concerns:**

- ~~Expression whitelist~~ - All expressions safe now
- ~~Preamble symbol checks~~ - Handled automatically
- ~~IndexVisitor compilation tracking~~ - Not needed

## Cross-File Module Instantiation

Module navigation differs from other features due to cross-compilation metadata lookup.

### Architecture

**Two-Compilation Design:**

- **PreambleManager**: Compiles all files once, extracts module metadata (ModuleInfo with ports/parameters)
- **OverlaySession**: Compiles current file + packages + interfaces only
- **SemanticIndex**: Indexes local symbols, queries PreambleManager for cross-file metadata

### Instance Elaboration in LSP Mode

**Architecture:** LSP mode uses `InstanceSymbol` for all module/interface/program instantiations with selective body elaboration via `InstanceFlags::SkipBody` flag.

**Two Entry Points:**

1. **Top-Level Instances** (semantic indexing for file being indexed)

   - Created via `InstanceSymbol::createDefault()`
   - No `SkipBody` flag → full body elaboration
   - Enables indexing all symbols in the file

2. **Nested Instances** (sub-module instantiations from syntax)
   - Created via `InstanceSymbol::fromSyntax()`
   - Automatically sets `SkipBody` flag if parent instance exists
   - Skips body elaboration (only parameters + ports from header)

**The SkipBody Flag Mechanism:**

The flag is needed because the decision point (nested vs top-level) and action point (skip body or not) are in different functions:

```cpp
// Decision point: fromSyntax() in InstanceSymbols.cpp:510-514
if (comp.hasFlag(CompilationFlags::LanguageServerMode) && parentInst) {
    flags |= InstanceFlags::SkipBody;  // Nested instance in LSP mode
}

// Action point: fromDefinition() in InstanceSymbols.cpp:1022-1024
// Skip body for nested module/program. Interface instances always elaborate.
if (!flags.has(InstanceFlags::SkipBody) ||
    definition.definitionKind == DefinitionKind::Interface) {
    // Add body members (always blocks, signals, nested instances, etc.)
}
```

**Why This Design:**

- `fromSyntax()` has access to parent hierarchy (can detect nested vs top-level)
- `fromDefinition()` does the actual elaboration (but doesn't know hierarchy context)
- Flag communicates "skip body" decision from context-aware code to elaboration code

**Module vs Interface Body Elaboration:**

The SkipBody flag applies differently to modules and interfaces:

**Modules:**

```systemverilog
module ALU #(parameter WIDTH = 8) (
    input logic [WIDTH-1:0] a, b,     // HEADER (params + ports)
    output logic [WIDTH-1:0] result
);
    logic [WIDTH-1:0] temp;           // BODY (internal signals)
    always_comb result = a + b;       // BODY (logic)
endmodule
```

For cross-file LSP, modules only need header (parameters + ports) to bind connections. Body members are internal implementation not accessed externally. SkipBody optimization skips body elaboration for nested modules.

**Interfaces:**

```systemverilog
interface data_if;
    logic [31:0] data;                // BODY (but accessed externally!)
    logic valid;
endinterface

module top;
    data_if bus();
    assign bus.data = 32'h1234;       // Member access requires body elaboration
endmodule
```

Interface body contains member declarations accessed externally via member selection (`.data`, `.valid`). Must always elaborate body for interface instances, even when nested. The condition `definition.definitionKind == DefinitionKind::Interface` overrides the SkipBody flag.

**Elaboration Logic:**

- SkipBody flag NOT set (top-level): Elaborate body
- SkipBody flag set + Module/Program: Skip body (header only)
- SkipBody flag set + Interface: Elaborate body (override, member access needed)

**Benefits:**

1. **Type context for port connections**: Uses `PortConnectionBuilder` which calls `Expression::bindArgument()` with proper target types, enabling assignment pattern type deduction
2. **Automatic array slicing**: Instance arrays like `counter inst[4]` automatically slice array ports
3. **Interface port handling**: Creates `ArbitrarySymbolExpression` for interface instances
4. **Proper validation**: Dimension checking, type validation at binding time
5. **Reuses existing Slang infrastructure**: No parallel code paths, all edge cases handled

**Performance:**

- Top-level: Full elaboration (same as normal compilation)
- Nested modules: Header only (parameters + ports, ~10-20 symbols)
- Nested interfaces: Full elaboration (needed for member access)
- Result: Sub-millisecond indexing per file even with deep module hierarchies

**Handler Requirements:**

SemanticIndex must handle `InstanceSymbol` for cross-file navigation:

1. Instance name self-definition
2. Parameter override expression navigation
3. Port connection expression navigation
4. Cross-file module/port/parameter type navigation via PreambleManager

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

**Common cause:** Using convenience methods like `getUsageLocation()` or `sourceRange()` that return full expression ranges instead of extracting the identifier token range from the syntax node.

**Pattern:** For compound syntax (parameterized types, scoped names, qualified identifiers):

```
full_syntax = syntax_node         // e.g., "Foo#(A, B)" or "pkg::Bar"
identifier  = syntax_node.identifier.range()  // Just "Foo" or "Bar"
```

**Detection:** If go-to-definition returns unexpected symbols, check if semantic entry ranges are too large. Binary search assumes non-overlapping ranges - violations cause wrong entries to be returned.

### Handler Patterns

- **Symbol handlers:** Process definitions, create self-references
- **Expression handlers:** Process usage, create cross-references
- **Always call** `visitDefault()` to continue traversal

### Definition Range Extraction

- Prefer precise name token ranges
- Handle optional syntax elements gracefully
- Fallback to `syntax.sourceRange()` when precise extraction impossible

### Complete Traversal for Compound Syntax

**Rule:** When indexing compound syntax nodes (parameterized types, array dimensions, port connections), ensure you index ALL child components, not just the primary identifier.

**Pattern:**

```
compound_syntax:
  1. Index primary identifier (the type/symbol name)
  2. Index parameters/arguments (visit all parameter value expressions)
  3. Index nested qualifiers (scoped names, array selectors)
```

**Common mistake:** Only indexing compound syntax from one context (e.g., only from expression handlers but not from type traversal handlers), causing incomplete indexing in certain declaration contexts.

**Detection:** If symbols in certain contexts (e.g., typedef declarations vs variable declarations) have different navigation behavior, check if all handlers traverse the same components.

## Debugging

### Systematic Debugging Workflow

**1. Check Basic Invariants First:**

Before investigating complex issues (cross-compilation, SourceManager, BufferID):

Add targeted logging to show: entry name, reference range, definition location

```
Log format: Found '{name}' at {pos}, def at {def_pos}, range [{start}..{end}]
```

Check:

- What semantic entries exist at the problem location?
- What are their ranges? (Too large? Overlapping?)
- Does the entry name match what you're looking for?

**2. Trust the Data:**

If logging shows unexpected entry at a position, the entry range is likely wrong - don't assume position calculation is at fault.

**3. Compare with Existing Patterns:**

For similar syntax, check how existing code extracts identifier ranges:

```
grep "identifier.range()" semantic_index.cpp
grep "SyntaxKind::YourSyntax" semantic_index.cpp
```

**4. Verify Before Implementing:**

Check what methods return before using them:

```
symbol.getUsageLocation()     // Might return full expression
syntax_node.identifier.range() // Returns just identifier token
```

### AST Investigation

```bash
mkdir -p debug
echo 'test code' > debug/test.sv
slang debug/test.sv --ast-json debug/ast.json
slang debug/test.sv --cst-json debug/cst.json
jq '.. | objects | select(.kind == "ClassName")' debug/cst.json
```

**Temporary Logging:**

Use `spdlog::debug()` for temporary logging, remove before committing.

**Finding Handlers:**

Indexing logic can span multiple handlers via `visitDefault()` calls. To locate handlers for an expression type:

- Search for the expression class name in `semantic_index.cpp`
- Add `spdlog::debug()` at handler entry to trace execution

### Common Issues and Root Causes

| Symptom                                | Common Cause            | Solution                                              |
| -------------------------------------- | ----------------------- | ----------------------------------------------------- |
| Wrong symbol returned                  | Overlapping ranges      | Extract identifier token range, not full syntax range |
| Symbols not navigable in some contexts | Incomplete traversal    | Index compound syntax components in all handlers      |
| Cross-file lookup fails                | Wrong SourceManager     | Use CreateLspLocation(symbol/expr, range)             |
| Spurious references                    | Wrong context filtering | Check structural properties (parent scope, flags)     |

**Test Development:** Failing test → targeted logging → minimal fix → cleanup.

## Type Reference Handling

See `LSP_TYPE_HANDLING.md` for typedef and type reference details.

## Examples

See subroutine implementation:

- Definition extraction: `SK::Subroutine` in `definition_extractor.cpp`
- Self-references: `handle(SubroutineSymbol&)` in `semantic_index.cpp`
- Call references: `handle(CallExpression&)` in `semantic_index.cpp`
- Tests: `definition_test.cpp`
