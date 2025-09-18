# SemanticIndex Architecture Refactoring

## Current Architecture Analysis

### What Works Well ✅

The unified SemanticIndex successfully achieves the core architectural goals:

1. **Single Traversal Efficiency**: One pass through the compilation via `preVisit` hook collects ALL needed information
2. **Automatic Deep Reference Collection**: The visitor automatically traverses deeply nested structures:
   ```
   module → generate blocks → always blocks → if/else → assignment → expression → signal reference
   ```
3. **O(1) Lookups**: After indexing, both `GetDocumentSymbols()` and `GetDefinitionRange()` are simple hash map lookups
4. **Universal Symbol Coverage**: Handles all 123+ SystemVerilog symbol types automatically

### Current Problems ❌

1. **Monolithic Implementation**: 687-line `semantic_index.cpp` mixing multiple responsibilities:
   - Symbol collection (preVisit hook)
   - Definition range extraction (per-symbol-type logic)
   - Document symbol tree building (hierarchical construction)
   - Reference tracking (NamedValueExpression handling)
   - Special case handling (enums, structs, generate blocks, interfaces)

2. **Mixed Collection Strategies**: Different LSP features have conflicting needs:
   - **Document Symbols**: Need hierarchical filtering for clean editor display
   - **References**: Need complete automatic traversal for deep expression analysis
   - **Definitions**: Need precise per-symbol-type syntax extraction

3. **Complex Test Suite**: Tests focus on internal implementation details rather than LSP behavior

## Architectural Principles

### Core Principle: Unified Single-Traversal Approach

**Keep the single traversal but separate concerns:**

```cpp
// Core: Single traversal orchestration
class SemanticIndex {
  // Storage (keep current)
  std::unordered_map<slang::SourceLocation, SymbolInfo> symbols_;
  std::unordered_map<SymbolKey, slang::SourceRange> definition_ranges_;
  std::unordered_map<slang::SourceRange, SymbolKey> reference_map_;

  // Visitor (simplified)
  class IndexVisitor {
    template<typename T> void preVisit(const T& symbol) {
      if constexpr (std::is_base_of_v<slang::ast::Symbol, T>) {
        ProcessSymbol(symbol);  // Delegate to specialized handlers
      }
    }
  };
};
```

### Separation Strategy

1. **Definition Extraction** (Whitelist Approach)
   - Each symbol type needs specific syntax handling
   - Module: `ModuleDeclarationSyntax.header->name`
   - TypeAlias: `TypedefDeclarationSyntax.name`
   - Variable: `syntax.sourceRange()`
   - Precise per-type extraction rules

2. **Reference Collection** (Automatic Deep Traversal)
   - NamedValueExpression: Uniform handling across all contexts
   - Automatic traversal finds references at any depth
   - Type references in variable declarations

3. **Document Symbol Building** (Hierarchical Construction)
   - Tree construction from flat symbol storage
   - Special handling for enums (add EnumValues as children)
   - Special handling for structs (add Fields as children)
   - URI filtering for multi-file support

## Refactored Architecture

### File Organization

```
semantic/
├── semantic_index.hpp/cpp              # Core storage + visitor orchestration
├── definition_extractor.hpp/cpp        # Per-symbol-type definition range extraction
├── document_symbol_builder.hpp/cpp     # Hierarchical tree construction
└── test_utilities.hpp/cpp              # Shared test helpers focused on LSP behavior
```

### Component Responsibilities

#### 1. SemanticIndex (Core)
```cpp
class SemanticIndex {
  // Storage
  std::unordered_map<slang::SourceLocation, SymbolInfo> symbols_;
  std::unordered_map<SymbolKey, slang::SourceRange> definition_ranges_;
  std::unordered_map<slang::SourceRange, SymbolKey> reference_map_;

  // Orchestration
  static auto FromCompilation(...) -> std::unique_ptr<SemanticIndex>;

  // Public LSP API
  auto GetDocumentSymbols(const std::string& uri) const -> std::vector<lsp::DocumentSymbol>;
  auto GetDefinitionRange(const SymbolKey& key) const -> std::optional<slang::SourceRange>;
  auto LookupSymbolAt(slang::SourceLocation loc) const -> std::optional<SymbolKey>;
};
```

#### 2. DefinitionExtractor (Symbol-Type Specialists)
```cpp
class DefinitionExtractor {
public:
  static auto ExtractDefinitionRange(const slang::ast::Symbol& symbol,
                                   const slang::syntax::SyntaxNode& syntax)
                                   -> slang::SourceRange;
private:
  static auto ExtractPackageRange(...) -> slang::SourceRange;
  static auto ExtractModuleRange(...) -> slang::SourceRange;
  static auto ExtractTypedefRange(...) -> slang::SourceRange;
  static auto ExtractVariableRange(...) -> slang::SourceRange;
  // ... per-symbol-type extraction methods
};
```

#### 3. DocumentSymbolBuilder (Hierarchical Tree Construction)
```cpp
class DocumentSymbolBuilder {
public:
  static auto BuildDocumentSymbolTree(
    const std::unordered_map<slang::SourceLocation, SymbolInfo>& symbols,
    const std::string& uri,
    const slang::SourceManager& source_manager) -> std::vector<lsp::DocumentSymbol>;

private:
  static auto CreateDocumentSymbol(const SymbolInfo& info) -> lsp::DocumentSymbol;
  static auto AttachChildrenToSymbol(...) -> void;
  static auto HandleEnumTypeAlias(...) -> void;    // Add enum values as children
  static auto HandleStructTypeAlias(...) -> void;  // Add struct fields as children
};
```

### Visitor Pattern (Simplified)

```cpp
class IndexVisitor : public slang::ast::ASTVisitor<IndexVisitor, true, true> {
public:
  explicit IndexVisitor(SemanticIndex* index,
                       const slang::SourceManager* source_manager,
                       DefinitionExtractor* def_extractor)
    : index_(index), source_manager_(source_manager), def_extractor_(def_extractor) {}

  // Universal symbol collection
  template <typename T>
  void preVisit(const T& symbol) {
    if constexpr (std::is_base_of_v<slang::ast::Symbol, T>) {
      ProcessSymbol(symbol);
    }
  }

  // Automatic reference collection
  void handle(const slang::ast::NamedValueExpression& expr);
  void handle(const slang::ast::VariableSymbol& symbol);

  template <typename T>
  void handle(const T& node) {
    this->visitDefault(node);  // Continue deep traversal
  }

private:
  void ProcessSymbol(const slang::ast::Symbol& symbol) {
    // Delegate definition extraction to specialist
    if (const auto* syntax = symbol.getSyntax()) {
      auto definition_range = def_extractor_->ExtractDefinitionRange(symbol, *syntax);
      // Store in index...
    }
    // Store symbol info...
  }
};
```

## Key Insights

### 1. Reference Collection is Already Optimal

The current automatic deep traversal via `visitDefault()` correctly finds references at any depth:

```systemverilog
module complex_example;
  logic signal_a, signal_b;

  generate
    for (genvar i = 0; i < 4; i++) begin : gen_loop
      always_comb begin
        if (some_condition) begin
          case (state)
            IDLE: result = signal_a & signal_b;  // References found automatically
            //               ^^^^^^^^   ^^^^^^^^
          endcase
        end
      end
    end
  endgenerate
endmodule
```

The `handle(NamedValueExpression&)` method catches **all** signal references regardless of nesting depth.

### 2. Definition Extraction Requires Specialization

Each symbol type has unique syntax structure requiring specific extraction logic:

```cpp
// Package definitions
case SK::Package:
  const auto& pkg_syntax = syntax.as<slang::syntax::ModuleDeclarationSyntax>();
  return pkg_syntax.header->name.range();

// Module definitions
case SK::Definition:
  const auto& mod_syntax = syntax.as<slang::syntax::ModuleDeclarationSyntax>();
  return mod_syntax.header->name.range();

// Typedef definitions
case SK::TypeAlias:
  const auto& typedef_syntax = syntax.as<slang::syntax::TypedefDeclarationSyntax>();
  return typedef_syntax.name.range();
```

### 3. Document Symbols Need Hierarchical Post-Processing

After flat collection, document symbols require tree construction with special handling:

- **Enums**: Add `EnumValue` symbols as children of `EnumType`
- **Structs**: Add `Field` symbols as children of `StructType`
- **Generate Blocks**: Handle template extraction vs iteration expansion
- **URI Filtering**: Only include symbols from requested document

## Benefits of Refactored Architecture

1. **Maintainability**: Clear separation of concerns makes each component easier to understand and modify
2. **Testability**: Each component can be tested independently with focused unit tests
3. **Extensibility**: Adding support for new symbol types only requires updating the appropriate specialist
4. **Performance**: Maintains O(1) lookups and single traversal efficiency
5. **Code Quality**: Smaller files with single responsibilities improve readability

## Migration Strategy

The refactoring maintains backward compatibility and all existing tests pass at each phase. The core storage structures and public API remain unchanged.