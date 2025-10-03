# LSP Type Handling Architecture

## Fundamental Difference: LSP vs Original Slang Library

**Original Slang Library Philosophy**: Resolve types to their final canonical form. Type resolution discards intermediate information and focuses only on the resolved result.

**LSP Requirements**: Preserve usage locations and reference chains for go-to-definition. Cannot discard intermediate type information because users need to navigate to original typedef definitions, not final resolved types.

This fundamental difference requires a completely different approach to type handling in LSP servers.

## TypeReferenceSymbol Architecture

**Core Problem**: Slang resolves typedefs to canonical types, losing usage locations needed for LSP go-to-definition.

**Solution**: TypeReferenceSymbol wrapper that:

- Wraps typedefs (TypeAlias), class types (ClassType), and generic class definitions (GenericClassDef)
- Preserves exact source location of type usage
- Delegates all type system queries to the wrapped type
- Integrates seamlessly with existing Slang type resolution

## Critical Integration Requirements

### 1. Type System Method Delegation

Every Type class method must delegate TypeReference cases to the wrapped type. Pattern: `return as<TypeReferenceSymbol>().getResolvedType().METHOD()`.

**Why TypeReferenceSymbol cannot simply return true for type checks**: It can wrap ANY typedef - both simple types (logic, int) AND complex types (structs, unions, multi-dimensional arrays). Delegation ensures semantic correctness.

### 2. Canonical Type Resolution ⚠️ CRITICAL

**Issue**: Nested typedefs create chains like `TypeAlias(local_t) → TypeReference → TypeAlias(data_t) → PackedStruct`.

**Fix Required**: `Type::resolveCanonical()` must unwrap TypeReference wrappers in the resolution loop. Without this, canonical resolution stops at TypeReference instead of reaching the final type, breaking `isIntegral()`, `isNumeric()`, and other type system operations.

**Location**: `slang/source/ast/types/Type.cpp::resolveCanonical()` - Add inner while loop to skip TypeReference kinds.

### 3. LSP Mode Enhanced Validation

LSP LanguageServerMode has enhanced validation that can invalidate statements when TypeReferenceSymbol integration is incomplete. Invalid statements prevent IndexVisitor traversal, silently breaking LSP features. This is why integration must be complete across ALL type system methods.

## Implementation Checklist

When adding TypeReferenceSymbol integration:

1. **Creation Point**: `Type::fromLookupResult()` wraps TypeAlias, ClassType, and GenericClassDef in TypeReferenceSymbol
2. **Method Delegation**: Add TypeReference cases to ALL Type class methods (isIntegral, isNumeric, etc.)
3. **Canonical Resolution**: Ensure `resolveCanonical()` unwraps TypeReference in typedef chains
4. **LSP Handlers**: Add IndexVisitor handlers for new expression types containing typedef references
5. **Test Coverage**: Test nested typedefs - they expose canonical resolution bugs

## Design Principles

### Always Use Symbol-Based Approach

Syntax is ambiguous for arrays - `data_t[3:0]` could be type dimensions or array selection. CST cannot distinguish these. Only AST/Symbol approach with semantic analysis works reliably.

### TypeReferenceSymbol Creation Point

`Type::fromLookupResult()` is the universal intervention point - captures typedef usage locations before type resolution discards them. Symbol-based, works for all typedef patterns.

## Key Implementation Files

**Slang Fork (type system integration)**:

- `source/ast/types/Type.cpp` - Canonical resolution with TypeReference unwrapping
- `source/ast/types/AllTypes.h/cpp` - TypeReferenceSymbol implementation
- `source/ast/Expression.h/cpp` - Syntax threading for cast expressions

**slangd (LSP handlers)**:

- `src/slangd/semantic/semantic_index.cpp` - Type traversal and indexing
- `test/slangd/semantic/type_reference_test.cpp` - Nested typedef regression test

## CST Range Limitations

CST groups semantically distinct constructs into single syntax nodes (e.g., type dimensions vs array selection both use `IdentifierSelectName`). LSP needs component-level ranges, but CST only provides composite ranges.

**Workaround**: `Type::fromLookupResult()` trims typedef ranges using semantic information (symbol name length), bypassing syntax tree limitations. This enables precise LSP navigation despite CST grouping.

## Type Traversal Strategy

### TypeReference vs TypeAlias Traversal

**TypeReference**: STOP at usage - it's a leaf node representing user's typedef reference. Traversing further creates duplicates.

**TypeAlias**: CONTINUE traversal into target type - may contain nested TypeReferences that need indexing for go-to-definition.

### Deduplication

Multiple symbols sharing the same type cause duplicate traversals. Track visited type syntax nodes to deduplicate at source level - types from the same location share syntax node pointers.

**Syntax Threading**: TypeReferences in cast expressions initially lacked syntax. Fixed by threading syntax parameter through `Expression::bindLookupResult()` → `Type::fromLookupResult()` → `TypeReferenceSymbol::create()`. All TypeReference instances now have syntax, enabling universal deduplication.
