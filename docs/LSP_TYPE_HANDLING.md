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

This architecture enables universal typedef go-to-definition across all SystemVerilog constructs while maintaining full compatibility with the existing Slang type system.