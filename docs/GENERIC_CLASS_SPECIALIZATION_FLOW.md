# Generic Class Specialization Flow Investigation

## Question

How does Slang handle this corner case:

- Generic class with function that has default argument containing a function call
- Specialization of that class in overlay compilation
- Calling the specialized class function without arguments

## Test Case

```systemverilog
// Preamble: uvm_pkg.sv
package uvm_pkg;
  virtual class uvm_event_base#(type T = int) extends uvm_object;
    T default_data;

    virtual function T get_default_data();
      return default_data;
    endfunction

    // Default argument calls get_default_data()
    virtual function void trigger(T data = get_default_data());
      // Trigger implementation
    endfunction
  endclass
endpackage

// Overlay: user_pkg.sv
package user_pkg;
  import uvm_pkg::*;

  class test_config extends uvm_object;
    uvm_event_base#(int) state_event;  // Specialization
  endclass

  class test_monitor extends uvm_object;
    test_config cfg;

    function void run_phase();
      cfg.state_event.trigger();  // Call without arguments
    endfunction
  endclass
endpackage
```

## Complete Flow

### Phase 1: Preamble Parsing

**File**: `uvm_pkg.sv`
**Compilation**: Preamble compilation
**SourceManager**: Preamble SM (BufferID 1 = uvm_pkg.sv)

1. Parser creates ClassDeclarationSyntax for `uvm_event_base#(type T)`
2. GenericClassDefSymbol created in preamble compilation
3. Method syntax includes FunctionDeclarationSyntax for `trigger(T data = get_default_data())`
4. Default argument syntax stored as ExpressionSyntax (not yet bound)

**Key**: Syntax is parsed, but expressions are NOT bound yet. Default argument is just syntax.

### Phase 2: Overlay Parsing and Type Resolution

**File**: `user_pkg.sv`
**Compilation**: Overlay compilation
**SourceManager**: Overlay SM (BufferID 1 = user_pkg.sv)

1. Parser encounters `uvm_event_base#(int) state_event;`
2. Overlay needs to resolve the type `uvm_event_base#(int)`
3. Calls `GenericClassDefSymbol::getSpecialization()` with parameter T=int

**Source**: ClassSymbols.cpp:963-1066

```cpp
const Type& GenericClassDefSymbol::getSpecialization(
    const ASTContext& context,
    const syntax::ParameterValueAssignmentSyntax& syntax) const {

  auto& comp = context.getCompilation();  // OVERLAY compilation
  auto scope = context.scope;

  // Create new ClassType in OVERLAY compilation
  auto classType = comp.emplace<ClassType>(comp, name, location);
  classType->genericClass = this;

  // ... parameter resolution ...

  // CRITICAL LINE: Populate from PREAMBLE syntax
  classType->populate(*scope, getSyntax()->as<ClassDeclarationSyntax>());

  return classType;
}
```

**Result**: New ClassType created in OVERLAY compilation, but populated from PREAMBLE ClassDeclarationSyntax

### Phase 3: Specialized Class Population

**Source**: ClassSymbols.cpp:163-182

```cpp
void ClassType::populate(const Scope& scope, const ClassDeclarationSyntax& syntax) {
  // scope = overlay scope
  // syntax = preamble ClassDeclarationSyntax

  setSyntax(syntax);  // Store preamble syntax pointer

  // Iterate through preamble syntax members
  for (auto member : syntax.items)
    addMembers(*member);  // Create symbols in overlay
}
```

**What happens**:

1. ClassType (overlay) iterates through ClassDeclarationSyntax items (preamble)
2. For each method in preamble syntax, creates SubroutineSymbol in overlay
3. SubroutineSymbol created with:
   - Parent: ClassType (overlay compilation)
   - Syntax: FunctionDeclarationSyntax (preamble syntax)
4. For each FormalArgumentSymbol in method:
   - Created in overlay compilation
   - Stores defaultValSyntax = preamble ExpressionSyntax for `get_default_data()`
   - Expression NOT bound yet

**Key observation**: Symbol tree is in overlay, but syntax pointers point to preamble

### Phase 4: Call Expression Without Arguments

**File**: `user_pkg.sv`
**Location**: `cfg.state_event.trigger();`

1. Slang sees call to `trigger()` with zero arguments
2. Method `trigger` has one parameter with default value
3. Needs to bind the default argument expression

**Source**: CallExpression creation logic checks for default arguments

### Phase 5: Default Argument Binding (THE CRITICAL STEP)

**Trigger**: First time default value is needed
**Source**: VariableSymbols.cpp:356-381

```cpp
const Expression* FormalArgumentSymbol::getDefaultValue() const {
  auto scope = getParentScope();  // SubroutineSymbol (in overlay)

  // Create ASTContext from the symbol's parent scope
  ASTContext context(*scope, LookupLocation::after(*this));

  // Bind expression using this context
  defaultVal = &Expression::bindArgument(
      getType(), direction, flags, *defaultValSyntax, context);

  return defaultVal;
}
```

**Breaking it down**:

1. `FormalArgumentSymbol` (overlay symbol) for parameter `data`
2. `getParentScope()` returns `SubroutineSymbol` (overlay)
3. `ASTContext context(*scope, ...)` creates context from overlay scope
4. Context derives compilation: `context.getCompilation()` = OVERLAY compilation
5. `defaultValSyntax` = ExpressionSyntax from PREAMBLE (the `get_default_data()` call)
6. Expression binding happens now

**Source**: Expression constructor (our Slang fork commit 42ed17f3)

```cpp
Expression(ExpressionKind kind, const Type& type,
           SourceRange sourceRange, Compilation& comp) :
    kind(kind), type(&type), sourceRange(sourceRange),
    compilation(&comp) {}  // Store compilation pointer
```

### Phase 6: CallExpression Creation for get_default_data()

When binding `get_default_data()` expression:

1. **Syntax**: `get_default_data()` - from preamble ExpressionSyntax
2. **Context**: ASTContext from overlay SubroutineSymbol
3. **Compilation**: context.getCompilation() = OVERLAY compilation

CallExpression created:

```cpp
CallExpression expr;
expr.kind = ExpressionKind::Call;
expr.compilation = &overlay_compilation;  // From context
expr.syntax = preamble_syntax;            // From defaultValSyntax
expr.sourceRange = preamble_syntax->sourceRange();  // BufferID from preamble!
```

### The Mismatch

**Expression members**:

- `expr.compilation` = Overlay Compilation (evaluation context)
- `expr.syntax` = Preamble CallExpressionSyntax (parse context)
- `expr.sourceRange` = Preamble SourceRange with preamble BufferIDs

**When indexed**:

- Slangd's CallExpression handler receives this expression
- `expr.compilation->getSourceManager()` returns overlay SourceManager
- `expr.sourceRange.start().buffer()` returns preamble BufferID

**If we tried to convert using overlay SM**:

```cpp
// WRONG - uses wrong SourceManager
auto loc = ToLspLocation(expr.sourceRange.start(),
                         *expr.compilation->getSourceManager());
// Converts preamble BufferID using overlay SM
// Result: Either crash (BufferID doesn't exist) or wrong file (BufferID exists but different file)
```

## Why BufferID Check Works

Our fix:

```cpp
// Skip expressions not in current file
if (expr.sourceRange.start().buffer() != current_file_buffer_) {
  this->visitDefault(expr);
  return;
}
```

**Why this works**:

1. Current file = `user_pkg.sv` = overlay BufferID 0 (offset 0)
2. Expression syntax from preamble = preamble BufferID 1024 (offset 1024)
3. BufferID values are genuinely different due to offset mechanism
4. Comparison fails → skip indexing

**BufferID Offset Solution**:

BufferIDs are just integers - without offset, both preamble and overlay would start at 1, causing collisions. We implemented BufferID offset in Slang's SourceManager:

- Preamble SourceManager: `setBufferIDOffset(1024)` → BufferIDs start at 1024
- Overlay SourceManager: `setBufferIDOffset(0)` → BufferIDs start at 0
- Internal indexing: `getBufferIndex(buffer)` subtracts offset to access `bufferEntries` array

This ensures no BufferID collision between compilations, making BufferID comparison reliable for filtering cross-file expressions.

**The expressions we skip**:

- Default argument expressions (syntax from preamble, evaluated in overlay)
- Any expression populated from preamble syntax into overlay symbols

**The expressions we index**:

- Expressions actually written in the current file
- User's call `cfg.state_event.trigger()` (overlay syntax, overlay BufferID)

## Architecture Lessons

### Why Slang Works This Way

1. **Symbol reuse**: Specialization populates from generic syntax to avoid duplicating AST
2. **Lazy binding**: Default arguments bound on-demand when needed, not during symbol creation
3. **Context-driven evaluation**: ASTContext determines where expression is evaluated (overlay)
4. **Syntax preservation**: Original syntax preserved for error messages and source locations

### Why Cross-Compilation is Tricky

1. **Evaluation context != Parse context**: Expression evaluated in overlay but parsed in preamble
2. **BufferID scope**: BufferIDs are per-SourceManager, not global
3. **No syntax→SM link**: SyntaxNode doesn't store which SourceManager created it
4. **Compilation determines SM**: expr.compilation determines which SM to use, but syntax came from different SM

### Our Solution

Don't try to convert cross-file expressions at all. Use BufferID to detect when an expression's syntax came from a different file than we're currently indexing, and skip it.

**Simple, direct, correct**: One-line comparison using source of truth (BufferID).

## Summary Table

| Component                                 | Preamble Phase       | Overlay Phase            | Evaluation Phase                     |
| ----------------------------------------- | -------------------- | ------------------------ | ------------------------------------ |
| **File**                                  | uvm_pkg.sv           | user_pkg.sv              | user_pkg.sv                          |
| **Compilation**                           | Preamble Compilation | Overlay Compilation      | Overlay Compilation                  |
| **SourceManager**                         | Preamble SM          | Overlay SM               | Overlay SM (wrong for default args!) |
| **BufferID values**                       | 1024+ (offset 1024)  | 0+ (offset 0)            | No collision                         |
| **ClassDeclarationSyntax**                | Created in preamble  | Reused from preamble     | N/A                                  |
| **GenericClassDefSymbol**                 | Created in preamble  | Accessed via import      | N/A                                  |
| **ClassType (specialized)**               | N/A                  | Created in overlay       | Accessed in overlay                  |
| **SubroutineSymbol (trigger)**            | N/A                  | Created in overlay       | Accessed in overlay                  |
| **FormalArgumentSymbol (data)**           | N/A                  | Created in overlay       | Accessed in overlay                  |
| **Default arg syntax**                    | Created in preamble  | Stored in overlay symbol | N/A                                  |
| **Default arg expression**                | N/A                  | N/A                      | Bound in overlay (mismatch!)         |
| **CallExpression for get_default_data()** | N/A                  | N/A                      | compilation=overlay, syntax=preamble |

## Key Insight

The fundamental issue: Slang's architecture assumes single-compilation where `expr.compilation->getSourceManager()` always owns `expr.sourceRange.buffer()`.

In cross-compilation (preamble + overlay), this assumption breaks when overlay symbols are populated from preamble syntax and expressions are lazily bound.

Our fix respects the semantic boundary: **A file's index should only contain that file's expressions**, determined by BufferID comparison.

**Critical enabler**: BufferID offset mechanism prevents ID collisions between separate SourceManagers, making direct BufferID comparison safe and reliable across compilations.
