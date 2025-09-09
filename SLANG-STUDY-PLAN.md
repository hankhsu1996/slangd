# Slang InstanceSymbol::createInvalid() Study Plan

## Background

We've identified that the LSP crashes are NOT caused by LINT mode, but specifically by `InstanceSymbol::createInvalid()` attempting to elaborate modules with unresolved parameters. Before implementing a full LSP mode in Slang, we want to understand if we can fix this specific function to handle LSP use cases gracefully.

## Current Issue Summary

**What we know:**
- Crash location: `InstanceSymbol::createInvalid()` → parameter resolution → port connection resolution
- Trigger: Single-file LSP analysis where module parameters are unresolved (normal scenario)
- Current workaround: Skip `createInvalid()` calls entirely in symbol visitor
- Impact: Working LSP but potentially missing some cross-module navigation features

**What we want to achieve:**
- Fix `createInvalid()` to handle unresolved parameters gracefully
- Enable full symbol indexing without crashes
- Maintain Slang's existing functionality for normal compilation

## Study Questions

### 1. Understanding InstanceSymbol::createInvalid()

**Questions:**
- What is the intended purpose of `createInvalid()`?
- Why does it need to resolve parameters for invalid instances?
- What's the difference between valid and invalid instances in Slang's model?
- Can we create "partially invalid" instances that skip parameter resolution?

**Study tasks:**
- [ ] Read Slang documentation on InstanceSymbol lifecycle
- [ ] Examine `createInvalid()` source code and comments
- [ ] Compare with `createValid()` or regular instance creation
- [ ] Identify where parameter resolution is triggered in the call chain

### 2. Parameter Resolution Pipeline

**Questions:**
- At what point does parameter resolution become mandatory?
- Can we defer or skip parameter resolution for LSP use cases?
- What happens if we allow unresolved parameters to remain unresolved?
- Are there existing flags or modes that control parameter resolution?

**Study tasks:**
- [ ] Trace the call stack from `createInvalid()` to the crash point
- [ ] Identify all functions involved in parameter resolution
- [ ] Look for existing conditional logic that might skip resolution
- [ ] Document the exact sequence that leads to the crash

### 3. Slang's Design Intent vs LSP Needs

**Questions:**
- Is `createInvalid()` supposed to work with incomplete compilation units?
- What assumptions does Slang make about parameter availability?
- How do other Slang tools handle incomplete/partial compilation?
- Are there existing "graceful degradation" patterns in Slang?

**Study tasks:**
- [ ] Review Slang's architecture documentation
- [ ] Examine how other Slang-based tools use `createInvalid()`
- [ ] Look for similar patterns where Slang handles missing information
- [ ] Identify Slang's error handling strategies for incomplete data

### 4. Potential Fix Approaches

**Questions:**
- Can we modify `createInvalid()` to accept "unresolvable" flag?
- Could we catch and handle parameter resolution failures gracefully?
- Is there a way to create "stub" parameters for missing values?
- Would a "lazy resolution" approach work for LSP use cases?

**Study tasks:**
- [ ] Prototype: Add error handling around parameter resolution
- [ ] Prototype: Create minimal "stub" parameters for missing values
- [ ] Prototype: Add conditional parameter resolution based on context
- [ ] Evaluate impact of each approach on existing Slang functionality

### 5. Testing & Validation Strategy

**Questions:**
- How do we test fixes without breaking existing Slang functionality?
- What are the minimal test cases that reproduce the issue?
- How do we validate that fixes work across different SystemVerilog patterns?
- What performance implications might our fixes have?

**Study tasks:**
- [ ] Create minimal reproduction case (single module with parameters)
- [ ] Set up test harness to validate Slang changes
- [ ] Design regression tests for existing Slang functionality
- [ ] Plan performance benchmarks for parameter resolution changes

## Unknowns & Research Areas

### High Priority Unknowns
1. **Why does `createInvalid()` need to resolve parameters at all?**
   - Is this fundamental to Slang's design or an implementation detail?
   - Could "invalid" instances skip parameter resolution by design?

2. **What exactly crashes during parameter resolution?**
   - Is it accessing null pointers, infinite recursion, or assertion failures?
   - Can we identify the exact failure mode and handle it?

3. **Are there existing Slang mechanisms for handling incomplete compilation?**
   - Does Slang have built-in graceful degradation patterns we can leverage?
   - How does LINT mode successfully avoid these issues?

### Medium Priority Unknowns
4. **What LSP features would we lose by avoiding `createInvalid()`?**
   - Which symbol navigation features require invalid instance traversal?
   - Can we quantify the functionality impact of our current workaround?

5. **How complex would a proper fix be?**
   - Would fixing this require deep architectural changes to Slang?
   - Could we implement a targeted fix with minimal impact?

### Lower Priority Questions
6. **How do other language servers handle similar issues?**
   - Do C++/Rust language servers have analogous problems with template/generic instantiation?
   - What patterns could we learn from other compiler-based LSPs?

## Success Criteria

**Minimum Success:**
- Understand why `createInvalid()` crashes with unresolved parameters
- Determine feasibility of fixing the function vs. working around it

**Ideal Success:**
- Implement a fix that allows `createInvalid()` to work gracefully with unresolved parameters
- Restore full symbol indexing functionality without crashes
- Maintain backward compatibility with existing Slang usage

**Decision Point:**
After this study, we should be able to decide:
- Fix `createInvalid()` directly, or
- Proceed with LSP mode implementation, or
- Accept current workaround as sufficient

## Study Timeline

**Phase 1: Understanding (1-2 days)**
- Questions 1-3: Understand current behavior and design intent

**Phase 2: Solution Design (1 day)**
- Question 4: Design and prototype potential fixes

**Phase 3: Validation (1 day)**
- Question 5: Test approaches and validate feasibility

**Decision Point: Choose path forward based on findings**

---
*This study plan should guide our investigation into whether we can solve the root cause instead of implementing workarounds.*