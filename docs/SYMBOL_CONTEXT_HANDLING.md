# LSP Symbol Reference Architecture

## Fundamental Difference: Compiler vs LSP Requirements

**Compiler Philosophy**: Resolve symbols to their final resolved form. Symbol resolution discards intermediate reference information and focuses only on the resolved result.

**LSP Requirements**: Preserve usage locations and reference chains for go-to-definition. Cannot discard intermediate reference information because users need to navigate from usage sites to original definitions.

This fundamental difference requires a completely different approach to symbol reference handling in LSP servers.

## Core Problem Pattern

**Universal Issue**: The same symbol instance appears in both definition and reference contexts during AST traversal, but LSP needs different behavior for each.

**Definition Context**: `task my_task(...)` - should create self-reference to definition location
**Reference Context**: `my_task(5, result)` - should create reference from call site to definition location

**Compiler Behavior**: Treats both contexts identically, resolving to the symbol's final form.
**LSP Requirement**: Must distinguish contexts to create correct reference relationships.

## Solution: Expression-Driven Reference Architecture

**Design Principle**: "Symbols define, Expressions reference"

- **Symbol handlers**: Create self-references for definitions only
- **Expression handlers**: Create references from usage sites to definitions  
- **Clean separation**: Each handler knows exactly what context it represents

## Why This Architecture is Correct

**Matches Slang Library Design**: 
- SubroutineSymbol represents the definition (has syntax, location, body)
- CallExpression represents the reference (contains pointer to SubroutineSymbol)

**Self-Explanatory**: No context ambiguity - each AST node type has clear semantics

**Scalable**: Same pattern works for all symbol types (variables, types, functions, etc.)

**No Duplicate Processing**: Clear separation of responsibilities between symbols and expressions

## Universal Application

This pattern applies to **all symbol types** where the compiler resolves references:
- SubroutineSymbol (functions/tasks) → CallExpression
- TypeAliasType (typedefs) → TypeReferenceSymbol  
- VariableSymbol (variables) → NamedValueExpression
- ParameterSymbol (parameters) → NamedValueExpression

## Range Precision Problem

**LSP Requirement**: Need precise ranges for individual components (just `data`, not `data[i]`).

**Expected**: Single AST node → Access syntax → Break down into components.
**Reality**: Even with syntax access, no way to decompose expressions into parts.

**Root Issue**: Slang's syntax tree itself doesn't provide component access.
- `ElementSelectExpression` syntax only has full range
- No child syntax nodes for individual tokens (`data`, `[`, `i`, `]`)

**Solution Pattern**: Range trimming based on symbol name length.
**Applied to**: Array access (`data[i]`), member access (`obj.field`), function calls with wrong ranges.

## Critical Design Rule

**Always use Expression-driven reference creation**: 
- Symbol handlers only process definitions
- Expression handlers process all references
- Never try to distinguish context within symbol handlers

This maintains clean architecture alignment with Slang's design while meeting LSP requirements for reference preservation.