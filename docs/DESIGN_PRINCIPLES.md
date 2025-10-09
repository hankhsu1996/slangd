# Design Principles

This document captures important lessons learned from solving complex problems in the slangd LSP implementation, particularly around Slang library integration.

## Core Philosophy

**Work with the system's design, not against it.**

Simple, elegant solutions exist when you understand and leverage the existing architecture. Complexity is often self-inflicted from not fully understanding what the library already provides.

## Working with the Slang Library

### 1. Study Existing Infrastructure First

Before adding new code or workarounds, thoroughly investigate what Slang already provides.

**Why this matters**: Slang has already solved many problems during compilation. The information you need often exists - you just need to find where it's stored.

**Case Study - Genvar Loop References**:

**Problem**: References to genvar loop iteration variables (e.g., `idx` in `for (idx = 0; idx < COUNT; idx++)`) need to resolve to the genvar declaration for go-to-definition.

**What we tried**:

1. **LSP Mode Approach**: Tried binding expressions in parent context to avoid temp variables

   - Failed: Violated SystemVerilog semantics (genvars are immutable, can't use in `idx++`)

2. **Manual Scope Traversal**: Tried searching up the scope chain to find GenerateBlockArray

   - Failed: Scope hierarchy didn't match expectations (StatementBlock → InstanceBody, not GenerateBlockArray)

3. **Location Matching**: Tried matching temp variable location with genvar location

   - Failed: GenerateBlockArray wasn't in the expected scope chain

4. **Visitor State Tracking**: Tried storing current GenerateBlockArray in visitor
   - Would work but creates tight coupling and fragile state management

**What worked**:

- Discovered Slang already did genvar lookup at `BlockSymbols.cpp:747`
- Found existing pattern: symbols can store pointers to related symbols (like how function parameters work)
- Added `getDeclaredSymbol()` to `VariableSymbol` (3 lines in header)
- Linked temp variable to genvar during construction (2 lines)
- LSP checks this pointer (1 simple conditional)

**Total solution**: ~20 lines of clean, maintainable code

**Key lesson**: The lookup we needed had already been performed. We just needed to save the result instead of trying to recreate it.

### 2. Follow Established Patterns

When you need to add new functionality, search the codebase for similar existing patterns.

**How to find patterns**:

- Search for similar functionality (e.g., "symbol redirection", "internal symbol")
- Look at how related features are implemented
- Check base classes and inheritance hierarchies
- Read Slang's documentation and headers

**Why this matters**:

- Consistency makes code maintainable
- Existing patterns are battle-tested
- You leverage compiler infrastructure instead of reinventing it

### 3. Understand Why Current Designs Exist

Before changing or working around existing behavior, understand the reason for the current design.

**Example - Genvar Temp Variables**:

- We initially saw the temp variable as a "problem" to work around
- Actually, it's necessary: genvar loops need mutable iteration, but genvars are immutable constants
- The design is correct - we just needed to link temp variable back to genvar

**Process**:

1. Ask: "Why does Slang do it this way?"
2. Read the code comments and surrounding context
3. Check SystemVerilog specification if needed
4. Only then decide if you need to change it

## Solution Quality Standards

### What "Elegant" and "Beautiful" Mean

These aren't vague aesthetic preferences - they're concrete quality criteria:

**Elegant code**:

- Follows existing patterns in the codebase
- Minimal lines of code (usually <50 for a feature)
- No special cases or conditional complexity
- Easy to understand and maintain
- Works with the system's design

**Non-elegant code** (red flags):

- Manual searching or lookup that duplicates library functionality
- Visitor state tracking for context
- Complex conditional logic with multiple fallbacks
- Long methods (>100 lines)
- Code that feels "hacky"

### Avoid Band-Aids

If your solution feels like a workaround or band-aid, keep searching for the elegant approach.

**Warning signs**:

- "We'll track this in a member variable during traversal"
- "We'll search up the scope chain to find..."
- "We'll use location matching to identify..."
- "We'll add a special case for..."

**Better approach**:

- "Where does Slang already store this relationship?"
- "What existing pattern handles similar cases?"
- "Can we save this information during construction?"

### Use Constraints as Design Guidance

When someone says "no manual searching" or "no state tracking", these aren't arbitrary restrictions - they're guideposts toward better design.

**Constraints point toward**:

- Using library-provided information instead of recreating it
- Storing relationships directly in data structures
- Stateless, declarative code
- Simple, composable solutions

### Prefer Positive Conditions

Write conditions that express WHEN to do something, not when to skip.

**Why this matters**:

- Positive conditions document intent: "traverse when in standalone mode" vs "skip when nested"
- Easier to verify correctness: check the main case is handled, not all exclusions
- Reduces cognitive load: reader knows when action happens

**When to use**: If a condition needs comments to clarify what it means, rewrite as positive.

### Write Comments That Explain "Why", Not "What"

Comments should explain **why** decisions were made, not what the code does.

**Use comments for:** Architectural constraints, design decisions, non-obvious behavior.

**Don't use comments for:** Stating the obvious, discussing past bugs, restating code in English.

**Principle:** If you need comments to explain what code does, refactor the code to be clearer instead.

## Problem-Solving Process

### 1. Periodically Stop and Verify

During implementation, regularly pause to verify you're solving the right problem.

**Questions to ask**:

- What is the actual problem? (Not symptoms, but root cause)
- Am I working with or against the system's design?
- Does this feel simple and natural, or complex and forced?
- Would I need to explain this with "well, because..." multiple times?

### 2. When Debugging Shows Unexpected Results

If debug output or tests show something unexpected, question your architectural assumptions rather than adding more complex logic.

**Example**:

- Expected: StatementBlock → GenerateBlockArray in scope chain
- Debug showed: StatementBlock → InstanceBody → CompilationUnit
- Wrong response: Add complex scope traversal logic
- Right response: Question assumption that scope chain contains the answer

### 3. Document the Journey

When you solve a difficult problem:

- Document what you tried and why it failed
- Explain the breakthrough insight
- Update architectural documentation
- Add design principles if you learned something generalizable

**Why**: Future developers (and AI agents) benefit from understanding the thought process, not just the final code.

## Examples and Case Studies

### Genvar Loop Reference Resolution

See "Working with the Slang Library" section above for detailed case study.

**Key files**:

- `slang/include/slang/ast/symbols/VariableSymbols.h`: Added `getDeclaredSymbol()/setDeclaredSymbol()`
- `slang/source/ast/symbols/BlockSymbols.cpp:760-768`: Links temp variable to genvar
- `slangd/src/slangd/semantic/semantic_index.cpp:350-367`: Uses the link for redirection

**Pattern**: Store symbol relationships during construction, use them during LSP traversal.

### Parameter Expression Preservation

See `docs/SEMANTIC_INDEXING.md` for details on symmetric expression storage pattern.

**Pattern**: Store expressions alongside computed values for LSP access without re-evaluation.

### Interface Instance Deduplication

Interfaces are fully elaborated when nested in modules, creating duplicate instances pointing to same syntax. Check parent scope to distinguish standalone (parent=CompilationUnit) vs nested. Only traverse standalone instances.

**Pattern**: Use positive conditions to express intent directly.

**Key files**:

- `slangd/include/slangd/semantic/semantic_index.hpp:185`: Handler declaration
- `slangd/src/slangd/semantic/semantic_index.cpp:1549-1570`: Parent scope check

## For AI Agents and Junior Engineers

If you're about to implement a new LSP feature or modify Slang integration:

1. **Read this document fully** - don't just skim
2. **Check the quick checklist** in CLAUDE.md before starting
3. **Study similar existing features** in the codebase first
4. **Ask "why" questions** about current designs before changing them
5. **Prefer simple solutions** - if it's complex, you might be missing something
6. **Document your learning** - update this file if you discover new principles

## When to Modify These Principles

These principles should evolve as we learn:

- Add new case studies when solving difficult problems
- Update examples when better patterns emerge
- Challenge principles if they lead to worse outcomes
- Keep the document focused on lessons that generalize

**Don't**: Add one-off solutions or project-specific details that don't teach broader lessons.
