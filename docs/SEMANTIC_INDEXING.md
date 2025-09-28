# Semantic Indexing Architecture

## Overview

The SemanticIndex is the core component that enables LSP features like go-to-definition, find references, and document symbols. It uses a unified architecture that combines symbol storage with reference tracking in a single pass.

**Key Components:**
- **SemanticIndex**: Main class that stores symbols and references
- **IndexVisitor**: AST visitor that processes symbols and expressions
- **DefinitionExtractor**: Extracts precise name ranges from syntax nodes
- **References Vector**: Stores source ranges and their target definitions

## How Go-To-Definition Works

1. **Symbol Processing**: `ProcessSymbol()` creates `SymbolInfo` entries in `symbols_` map
2. **Reference Creation**: Various `handle()` methods call `CreateReference()` to populate `references_` vector
3. **Lookup**: `LookupDefinitionAt()` searches `references_` for ranges containing the cursor position
4. **Result**: Returns the target definition range from the matching reference entry

## Adding New Symbol Support

### 1. Definition Extraction

Add support in `DefinitionExtractor::ExtractDefinitionRange()`:

```cpp
case SK::MySymbol:
  if (syntax.kind == SyntaxKind::MyDeclaration) {
    return syntax.as<slang::syntax::MyDeclarationSyntax>()
        .name.range();  // or .prototype->name->range() for complex cases
  }
  break;
```

**Key Points:**
- Extract the precise name token range, not the full declaration
- Use appropriate syntax node type (check Slang's `AllSyntax.h`)
- Handle null checks for optional syntax elements

### 2. Self-Definition Handler

Add a `handle()` method for the symbol type to enable go-to-definition on the symbol itself:

```cpp
void SemanticIndex::IndexVisitor::handle(const slang::ast::MySymbol& symbol) {
  if (symbol.location.valid()) {
    if (const auto* syntax = symbol.getSyntax()) {
      auto definition_range = DefinitionExtractor::ExtractDefinitionRange(symbol, *syntax);
      CreateReference(definition_range, symbol);
    }
  }
  this->visitDefault(symbol);
}
```

**Don't forget:**
- Add declaration to `semantic_index.hpp`
- Call `visitDefault()` to continue AST traversal

### 3. Reference Handler (for expressions that use the symbol)

Add handlers for expression types that reference your symbol:

```cpp
void SemanticIndex::IndexVisitor::handle(const slang::ast::MyExpression& expr) {
  // Extract target symbol from expression
  const auto* target = expr.getTargetSymbol();
  CreateReference(expr.sourceRange, *target);
  this->visitDefault(expr);
}
```

**Common Expression Types:**
- `NamedValueExpression`: Variable/symbol references
- `CallExpression`: Function/task calls
- `ConversionExpression`: Type casts
- Custom expression types for specific language constructs

### 4. Include Required Headers

Add necessary includes to `semantic_index.cpp`:

```cpp
#include <slang/ast/expressions/MyExpression.h>
#include <slang/ast/symbols/MySymbols.h>
```

### 5. Test Coverage

Create comprehensive tests in `definition_test.cpp`:

```cpp
TEST_CASE("SemanticIndex my_symbol self-definition lookup works") {
  // Test go-to-definition on the symbol definition itself
  fixture.AssertGoToDefinition(*index, code, "my_symbol", 0, 0);
}

TEST_CASE("SemanticIndex my_symbol reference go-to-definition works") {
  // Test go-to-definition on a usage/reference of the symbol  
  fixture.AssertGoToDefinition(*index, code, "my_symbol", 1, 0);
}
```

**Test Parameters:**
- `reference_index`: Which occurrence to click on (0 = first, 1 = second, etc.)
- `definition_index`: Which occurrence should be the target (usually 0 for definition)

## Architecture Patterns

### Symbol vs Reference Distinction
- **Symbols** (`symbols_` map): Indexed by location, store symbol metadata
- **References** (`references_` vector): Store source range → target definition mappings

### Handler Method Patterns
- **Symbol handlers**: Process symbol definitions, create self-references
- **Expression handlers**: Process symbol usage, create cross-references
- **Always call** `visitDefault()` to continue traversal

### Definition Range Extraction
- Prefer precise name token ranges over full syntax ranges
- Handle optional syntax elements gracefully
- Fall back to `syntax.sourceRange()` when precise extraction isn't possible

## Debugging Tips

### AST Investigation
Use the debug directory for AST exploration:
```bash
mkdir -p debug
echo 'your test code' > debug/test.sv
slang debug/test.sv --ast-json debug/ast.json
# Search the JSON for symbol kinds and expression types
```

### Common Issues
1. **"LookupDefinitionAt failed"**: Missing reference creation (need expression handler)
2. **"No symbol found"**: Missing symbol processing (need definition extraction)
3. **Wrong definition target**: Check reference creation logic or test indices

### Test Development
1. Start with failing tests
2. Add debug logging to understand AST structure
3. Implement minimal changes to make tests pass
4. Clean up debug code

## Examples

See the subroutine implementation for a complete example:
- **Definition extraction**: `SK::Subroutine` case in `definition_extractor.cpp`
- **Self-references**: `handle(SubroutineSymbol&)` in `semantic_index.cpp`  
- **Call references**: `handle(CallExpression&)` in `semantic_index.cpp`
- **Test coverage**: Multiple test cases in `definition_test.cpp`

## Known Limitations

### SystemVerilog Generate Variables (Genvar References)

**Current Support**: 
- ✅ Genvar self-definitions work (`genvar i;` - clicking on `i` goes to declaration)
- ✅ Generate block self-definitions work (clicking on named generate blocks)
- ❌ **Genvar references in expressions do NOT work**

**Examples of unsupported references**:
```systemverilog
module example;
  genvar i;
  generate
    for (i = 0; i < 4; i = i + 1) begin : gen_loop
         ^     ^     ^           // ← These references fail
      logic [i:0] bus;
             ^                  // ← This reference also fails  
    end
  endgenerate
  
  always_comb begin
    case (i)  // ← This reference fails too
      // ...
    endcase
  end
endmodule
```

**Root Cause**: Genvar references get resolved to constant literals (`IntegerLiteral`) during compilation, losing the original symbol reference information needed for go-to-definition.

**Technical Challenge**: Unlike regular variables that use `NamedValueExpression` (preserving symbol references), genvars resolve directly to constants. The LSP-side replay approach used for parameters fails because:
1. **Scope limitations**: Genvar references can appear anywhere, not just in type dimensions
2. **Resolution timing**: Genvars are resolved during generate block elaboration, before LSP processing
3. **Performance concerns**: Would require aggressive expression rebinding across entire syntax trees

**Future Solution**: This limitation will likely require modifications to the Slang compiler library to preserve original genvar symbol references in an LSP-compatible way, following the existing LSP modifications in the Slang fork (`memberNameRange`, `LanguageServerMode`, etc.).