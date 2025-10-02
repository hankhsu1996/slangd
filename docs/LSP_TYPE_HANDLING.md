# LSP Type Handling Architecture

## Fundamental Difference: LSP vs Original Slang Library

**Original Slang Library Philosophy**: Resolve types to their final canonical form. Type resolution discards intermediate information and focuses only on the resolved result.

**LSP Requirements**: Preserve usage locations and reference chains for go-to-definition. Cannot discard intermediate type information because users need to navigate to original typedef definitions, not final resolved types.

This fundamental difference requires a completely different approach to type handling in LSP servers.

## TypeReferenceSymbol Architecture

### Core Problem

```systemverilog
typedef logic [7:0] data_t;
data_t my_var;  // User clicks here - must go to typedef definition, not logic[7:0]
```

**Slang's Default Behavior**: `data_t` resolves directly to `PackedArrayType(ScalarType)`, losing the typedef reference.

**LSP Requirement**: Preserve the typedef reference while maintaining type resolution functionality.

### Solution: TypeReferenceSymbol Wrapper

```cpp
// LSP Type Structure
data_t my_var → TypeReferenceSymbol(resolvedType: TypeAlias(name: "data_t"))

// For arrays: data_t[3:0] my_array
PackedArrayType(
    elementType: TypeReferenceSymbol(resolvedType: TypeAlias(name: "data_t"))
)
```

**Key Design Principles**:
- TypeReferenceSymbol wraps ONLY the typedef, not composite types
- Preserves exact source location of typedef usage
- Delegates all type system operations to wrapped type
- Maintains semantic correctness with existing Slang infrastructure

## Critical Integration Requirements

### Type System Method Delegation

TypeReferenceSymbol must integrate with ALL existing Slang type system methods. Cannot simply add `TypeReference` to boolean checks - must delegate appropriately:

```cpp
// WRONG: Breaks semantic correctness
bool Type::isSimpleType() const {
    case SymbolKind::TypeReference:
        return true;  // Too broad - breaks complex typedef validation
}

// CORRECT: Maintains semantic correctness  
bool Type::isSimpleType() const {
    case SymbolKind::TypeReference:
        return as<TypeReferenceSymbol>().getResolvedType().isSimpleType();
}
```

**Why This Matters**: TypeReferenceSymbol can wrap any typedef - simple types (logic, int) OR complex types (structs, multi-dimensional arrays). Delegation ensures validation remains semantically correct.

## Compilation Mode Differences

### Standalone Slang vs LSP Mode

**Standalone slang**: Basic compilation, minimal validation
**LSP LanguageServerMode**: Enhanced validation that catches type system inconsistencies

### LSP Mode Validation

**Key Insight**: LSP LanguageServerMode has enhanced validation that can cause statements to become Invalid when TypeReferenceSymbol integration is incomplete. Invalid statements prevent IndexVisitor traversal, causing missing handler calls.

**Critical Requirement**: TypeReferenceSymbol must integrate with ALL existing Slang type system methods to maintain validation compatibility.

## Common Pitfalls

### 1. Type System Integration Oversight

**Problem**: Adding TypeReferenceSymbol without updating ALL type system methods

**Symptom**: Compilation failures, validation errors in LSP mode

**Solution**: Systematically audit all Type class methods for TypeReference support

### 2. Syntax vs Symbol Confusion

**Problem**: Using syntax-based approaches for typedef references

**Why It Fails**: Syntax is ambiguous for complex array constructs. `data_t[3:0]` could be array dimensions or array element selection.

**Solution**: Always use AST/Symbol approach with semantic analysis

## Implementation Checklist

When adding new typedef usage support:

1. **Type Resolution**: Ensure TypeReferenceSymbol is created in `Type::fromLookupResult()`
2. **Type System Integration**: Update relevant Type class methods for delegation
3. **Handler Implementation**: Add IndexVisitor handlers for new expression types
4. **Test Coverage**: Test both simple and complex typedef scenarios

## Design Principles

### Syntax vs Symbol Approach

**FUNDAMENTAL RULE**: Always use AST/Symbol approach, NEVER syntax for typedef references.

**The Problem**: Syntax is ambiguous for array constructs:
```systemverilog
typedef logic [7:0] data_t;
input data_t [3:0][1:0] multi_array;  // Syntax: IdentifierSelectName (ambiguous)
//    ^~~~~~~ AST: TypeAliasType (precise)
```

**Why Syntax Fails**:
- CST cannot distinguish array dimensions vs array selections
- Both `data_t[3:0][1:0]` (dimensions) and `arr[3:0][1:0]` (selections) use identical syntax
- Only semantic analysis resolves this ambiguity

**Correct Pattern**:
```cpp
// SAFE: Let AST traversal find TypeAliasType naturally
TraverseType(type);  // Recursive, will encounter TypeAliasType

// DANGEROUS: Syntax-based extraction
if (type_syntax->kind == SyntaxKind::NamedType) { /* FAILS for multi-dim */ }
```

### Type::fromLookupResult() Integration Point

**Critical Discovery**: The exact point where typedef usage locations were lost:

```cpp
const Type& Type::fromLookupResult(Compilation& compilation, const LookupResult& result,
                                   SourceRange sourceRange, const ASTContext& context) {
    const Symbol* symbol = result.found;  // TypeAlias definition
    // CRITICAL: Usage location (sourceRange) must be preserved here
    return TypeReferenceSymbol::create(*symbol, sourceRange, compilation);
}
```

**Why This Works**:
- Exact intervention point - captures usage before resolution discards it
- Symbol-based - no syntax ambiguity issues
- Universal - works for all typedef usage patterns
- Minimal - integrates cleanly with existing type resolution

## Architecture Files

**Core Type System**:
- `/home/shou-li/slang/source/ast/types/Type.cpp` - Type system method delegation
- `/home/shou-li/slang/include/slang/ast/symbols/TypeSymbols.h` - TypeReferenceSymbol definition

**LSP Integration**:
- `/home/shou-li/slangd/src/slangd/semantic/semantic_index.cpp` - IndexVisitor handlers
- `/home/shou-li/slangd/include/slangd/semantic/semantic_index.hpp` - Handler declarations

**Expression Validation**:
- `/home/shou-li/slang/source/ast/expressions/ConversionExpression.cpp` - Cast expression validation
- Other expression handlers that use type validation

## Critical Syntax Tree Limitations

### Fundamental CST Grouping Issue

**Root Cause**: Slang's Concrete Syntax Tree (CST) treats large chunks of different syntax as single syntax nodes, making precise range extraction impossible at the syntax level.

**Core Problem**: The syntax tree level cannot distinguish between semantically different constructs:
- Type array dimensions: `control_t [WIDTH-1:0]` 
- Array element selection: `array[WIDTH-1:0]`

Both create identical `IdentifierSelectName` syntax nodes despite having completely different semantic meanings.

**Architectural Limitation**: LSP requires component-level ranges for precise navigation, but CST provides only composite ranges. This is not an AST issue - it's a fundamental limitation of how syntax trees group tokens.

**Universal Impact**: This limitation affects any construct where multiple semantic components are grouped into single syntax nodes:
- Typedef references with array dimensions
- Interface port declarations with modport selection
- Method calls with complex parameter expressions
- Any syntax where precise sub-component ranges are needed for LSP navigation

### TypeReferenceSymbol Range Trimming Solution

**Workaround Strategy**: Since syntax-level extraction is fundamentally impossible, trim ranges using semantic information (symbol name length).

**Implementation**: 5-line fix in `Type::fromLookupResult()` trims typedef ranges to exact name boundaries, bypassing syntax tree limitations entirely.

**Design Principle**: When CST cannot provide precise ranges, use semantic analysis to reconstruct component boundaries rather than accepting broad composite ranges.

**Impact**: Resolves typedef navigation issues by working around fundamental syntax tree grouping limitations.

## Type Traversal Strategy

### TypeAlias vs TypeReference: Different Traversal Rules

**Critical Distinction**: TypeAlias and TypeReference serve different purposes and require different traversal strategies in LSP.

#### TypeReference (Variable/Cast Usage)
```systemverilog
typedef logic [7:0] byte_t;
byte_t my_var;        // TypeReference
byte_t'(some_value);  // TypeReference
```

**Traversal Rule**: **STOP at TypeReference - do NOT traverse resolved type**

```cpp
case SymbolKind::TypeReference: {
  const auto& type_ref = type.as<TypeReferenceSymbol>();
  // Create semantic entry for this usage
  AddReference(type_ref.getUsageLocation(), typedef_definition);
  // STOP HERE - do not traverse into resolved TypeAlias
  break;  // No visitDefault or recursive traversal
}
```

**Why**: TypeReference is a leaf node for LSP. It represents a user's reference to a typedef. Traversing further would create duplicate entries.

#### TypeAlias (Typedef Declaration)
```systemverilog
typedef byte_t word_t;  // TypeAlias using another typedef
```

**Traversal Rule**: **DO traverse the target type** to find nested typedef references

```cpp
case SymbolKind::TypeAlias: {
  const auto& type_alias = type.as<TypeAliasType>();
  // Create self-definition entry
  AddDefinition(type_alias, ...);
  // CONTINUE - traverse target to find nested TypeReference
  TraverseType(type_alias.targetType.getType());
  break;
}
```

**Why**: The target type may contain another TypeReference (e.g., `byte_t` in example). We need to index that reference so users can click `byte_t` and navigate to its definition.

### Deduplication Strategy

**Problem**: Multiple symbols can share the same type, causing duplicate traversals:

```systemverilog
typedef logic [7:0] byte_t;
byte_t var_a, var_b;  // Same type, two variables
```

Without deduplication, we'd traverse `byte_t`'s type twice, creating duplicate semantic entries.

**Solution**: Track visited type syntax nodes

```cpp
class IndexVisitor {
  std::unordered_set<const syntax::SyntaxNode*> visited_type_syntaxes_;

  void TraverseType(const Type& type) {
    // Skip if already visited this type's syntax
    if (const auto* syntax = type.getSyntax()) {
      if (!visited_type_syntaxes_.insert(syntax).second) {
        return;  // Already processed
      }
    }
    // Continue with type traversal...
  }
};
```

**Key Insight**: Types created from the same source location share the same syntax node pointer. By tracking syntax nodes, we deduplicate at the source level.

**Edge Case - Cast Expressions**: TypeReferences in casts initially lacked syntax (created dynamically). Fixed in Slang fork by passing syntax through `Expression::bindLookupResult()` → `Type::fromLookupResult()` → `TypeReferenceSymbol::create()`.

### Slang Fork Modifications for Syntax Preservation

**Problem**: TypeReferenceSymbol was created without syntax nodes, breaking deduplication.

**Root Cause**: Type creation from expressions (casts) didn't have access to syntax context.

**Solution**: Thread syntax through the call chain:

1. **Expression.h/cpp**: Add `syntax` parameter to `bindLookupResult()`
2. **Type.h/cpp**: Add `syntax` parameter to `fromLookupResult()`
3. **AllTypes.h/cpp**: Add `syntax` parameter to `TypeReferenceSymbol` constructor, call `setSyntax(*syntax)`

**Result**: ALL TypeReferenceSymbol instances now have syntax, enabling universal deduplication.

```cpp
// In Expression::bindLookupResult
const Type& resultType = Type::fromLookupResult(comp, result, sourceRange, context, syntax);

// In Type::fromLookupResult
if (isTypedefUsage) {
  finalType = &TypeReferenceSymbol::create(*finalType, sourceRange, syntax, compilation);
}

// In TypeReferenceSymbol constructor
TypeReferenceSymbol::TypeReferenceSymbol(..., const syntax::SyntaxNode* syntax) {
  setSyntax(*syntax);  // Preserve syntax for deduplication
}
```

This architecture enables universal typedef go-to-definition across all SystemVerilog constructs while maintaining full compatibility with the existing Slang type system.