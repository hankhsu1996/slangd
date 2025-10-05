# LSP Type Handling Architecture

## Core Architectural Difference

**Slang Library**: Resolves types to canonical form, discarding intermediate typedef information.

**LSP Requirement**: Preserve typedef usage locations for go-to-definition navigation.

This fundamental mismatch drives all type handling patterns in slangd.

## TypeReferenceSymbol: Preserving Type Usage Locations

**Problem**: Slang's `Type::fromLookupResult()` resolves typedefs to canonical types, losing the usage location needed for LSP navigation.

**Solution**: Lightweight wrapper that:

- Wraps TypeAlias/ClassType/GenericClassDef at usage sites
- Stores precise source location of the type reference
- Delegates all type system queries to wrapped type (maintains semantic correctness)

**Integration Requirements**:

1. **Method Delegation**: Every Type method must delegate TypeReference cases: `return as<TypeReferenceSymbol>().getResolvedType().METHOD()`
2. **Canonical Resolution**: `Type::resolveCanonical()` must unwrap TypeReference in resolution loop (critical for nested typedefs)
3. **LSP Mode Validation**: Incomplete integration breaks statement validation, silently preventing IndexVisitor traversal

**Creation Point**: `Type::fromLookupResult()` - universal intervention before type resolution

**Files**:

- `slang/source/ast/types/Type.cpp` - canonical resolution
- `slang/source/ast/types/AllTypes.{h,cpp}` - TypeReferenceSymbol implementation

## Type Traversal Patterns

### TypeReference vs TypeAlias

- **TypeReference**: Leaf node (usage location) - STOP traversal to avoid duplicates
- **TypeAlias**: Definition node - CONTINUE into target type (may contain nested TypeReferences)

### Syntax-Based Deduplication

Multiple symbols sharing same type syntax → duplicate traversal. Track `visited_type_syntaxes_` to deduplicate at source level (types from same location share syntax pointers).

**Syntax Threading**: Cast expressions lacked syntax initially. Fixed by threading syntax through `Expression::bindLookupResult()` → `Type::fromLookupResult()` → `TypeReferenceSymbol::create()`.

## Elaborated Types: Module/Instance Pattern

**Critical Discovery**: Just like ModuleSymbol (template) vs InstanceSymbol (elaborated body), Slang separates class definitions from instances:

- **GenericClassDefSymbol**: Parameterized class template (e.g., `class C #(int P);`) - does NOT expose body
- **ClassType**: Serves dual roles depending on context:
  1. **Standalone definition** for non-parameterized classes (e.g., `class C;`) - visited via PATH 3 (CompilationUnit)
  2. **Specialization instance** for parameterized classes - visited via GenericClassDefSymbol's `getDefaultSpecialization()`

**Why ClassType handlers need duplicate logic**: Class-level features (extends, members) can appear in both parameterized and non-parameterized class definitions, requiring handlers in both `ClassType` (role #1) and `GenericClassDefSymbol` (role #2 via default specialization).

### ClassType Traversal Strategy

**Design Principle**: ClassType body traversal happens via two paths:

- **PATH 3**: Non-parameterized ClassType (`genericClass == nullptr`) visited directly from CompilationUnit
- **GenericClassDefSymbol**: Parameterized classes call `getDefaultSpecialization()` and explicitly visit the resulting ClassType

**Why This Works**:

- Type references (variables, parameters) don't need body traversal - handled by TypeReferenceSymbol wrapping
- TraverseType() already skips ClassType traversal (no automatic body traversal from type usage)
- No syntax deduplication needed (each definition visited once from its source file)

### Pattern Categories

**TypeReferenceSymbol** (lightweight wrapper pattern):

- Created per-usage, never cached
- Variable declarations, typedef references, type casts

**Explicit Visit Pattern** (elaborated types):

- GenericClassDefSymbol creates default ClassType and explicitly visits it
- Module/class bodies only indexed when definition is in current file
- No automatic traversal from type references
- Store ClassType scope in SemanticEntry to avoid re-computing `getDefaultSpecialization()` in DocumentSymbolBuilder

## Design Principles

**Symbol-Based Approach**: Syntax is ambiguous (`data_t[3:0]` = type dimension or array selection?). Only AST/Symbol with semantic analysis works reliably.

**CST Range Limitations**: CST groups semantic constructs into composite nodes. `Type::fromLookupResult()` trims ranges using symbol name length to achieve precise navigation.

**No Overlapping Ranges**: Fix root cause in reference creation, not disambiguation logic in lookup.
