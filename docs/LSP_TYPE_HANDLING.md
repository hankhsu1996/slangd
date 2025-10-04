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

- **GenericClassDefSymbol**: Template only - does NOT expose class body as children
- **ClassType**: Elaborated instance - contains parameters, properties, methods as visitable children

**LSP Solution**: GenericClassDefSymbol handler calls `getDefaultSpecialization()` to create temporary ClassType with default parameters, then explicitly visits it to index the body.

### ClassType Traversal Strategy

**Design Principle**: ClassType body traversal ONLY via explicit visit from GenericClassDefSymbol handler.

**Why This Works**:
- Type references (variables, parameters) don't need body traversal - handled by TypeReferenceSymbol wrapping
- TraverseType() already skips ClassType traversal (line 415-419 in semantic_index.cpp)
- Only GenericClassDefSymbol calls `default_type->visit(*this)` to index body members
- This eliminates need for syntax deduplication in ClassType handler (no duplicate traversal possible)

**Benefits**:
- No URI filtering needed (only explicit visits from current file's GenericClassDefSymbol)
- No `visited_type_syntaxes_` tracking for ClassType (still used for other types like PackedArrayType)
- Cleaner separation: definition indexing vs. type reference handling

### Pattern Categories

**TypeReferenceSymbol** (lightweight wrapper pattern):

- Created per-usage, never cached
- Variable declarations, typedef references, type casts

**Explicit Visit Pattern** (elaborated types):

- GenericClassDefSymbol creates default ClassType and explicitly visits it
- Module/class bodies only indexed when definition is in current file
- No automatic traversal from type references

## Design Principles

**Symbol-Based Approach**: Syntax is ambiguous (`data_t[3:0]` = type dimension or array selection?). Only AST/Symbol with semantic analysis works reliably.

**CST Range Limitations**: CST groups semantic constructs into composite nodes. `Type::fromLookupResult()` trims ranges using symbol name length to achieve precise navigation.

**No Overlapping Ranges**: Fix root cause in reference creation, not disambiguation logic in lookup.
