# Implementation Progress: Config-Driven to Auto-Discovery Refactor

## Status Overview
- **Started**: 2025-09-08
- **Current Phase**: Crash Resolution & Targeted Fix
- **Overall Progress**: 40% (Auto-discovery working, crash fixed with targeted solution)

## Phase 1: Setup & ConfigManager Refactor (100% Complete)

### 1.1 Slang Integration Setup
- [x] **~~Convert Slang to git submodule~~ Use separate repository approach** (COMPLETED)
  - [x] ~~Add Slang as git submodule~~ Determined incompatible with Bazel bzlmod
  - [x] Use git_override in MODULE.bazel pointing to separate slang repository  
  - [x] Verify build works with separate repository approach
  - [x] Build system verified working (both local and CI)

### 1.2 ~~Make ConfigManager Optional~~ Keep ConfigManager Always Present (2/2 Complete)
**REVISED APPROACH**: Keep file discovery unchanged, focus on includes/defines only

- [x] **Verify current auto-discovery works** 
  - [x] Test that `GetSourceFiles()` fallback properly scans workspace without .slangd
  - [x] Ensure `FindSystemVerilogFilesInDirectory()` finds all relevant files (1320 files found)
  - [x] Check performance with larger workspaces (working without crashes)
  
- [x] **Ensure includes/defines work without heavy config**
  - [x] Verify DocumentManager gets empty includes/defines when no .slangd
  - [x] Test that compilation works with auto-discovered files + empty config
  - [x] Document that file lists in .slangd are optional enhancement

### 1.3 Test Basic Auto-Discovery (3/3 Complete)
- [x] **Create test workspace** without `.slangd` file (tested with large 1320-file workspace)
- [x] **Verify basic functionality** works (file discovery, parsing, basic diagnostics)
- [x] **Document any issues** found with auto-discovery mode (createInvalid crash documented and fixed)

## Phase 2: Slang LSP Mode Implementation (0% Complete)

### 2.1 Slang Analysis & Understanding (0/5 Complete)
- [ ] **Study Slang compilation modes**
  - [ ] Understand current LintMode implementation
  - [ ] Identify where LintMode breaks symbol indexing
  - [ ] Map out compilation pipeline and visitor interaction
  
- [ ] **Analyze current symbol indexing issues**
  - [ ] Reproduce submodule instantiation segfaults
  - [ ] Identify specific AST nodes missing in LintMode
  - [ ] Document exact failure modes

- [ ] **Research Slang visitor patterns**
  - [ ] Understand `slang::ast::makeVisitor` usage in `SymbolIndex::FromCompilation`
  - [ ] Identify which visitor callbacks fail with incomplete AST
  - [ ] Document required AST completeness for symbol indexing

- [ ] **Study Slang compilation flags**
  - [ ] Map out all `CompilationFlags` options
  - [ ] Understand interaction between flags and AST generation
  - [ ] Identify minimal requirements for visitor-compatible AST

- [ ] **Design LSP mode requirements**
  - [ ] Define what "partial compilation" means for LSP needs
  - [ ] Specify which symbols must be preserved vs can be missing
  - [ ] Plan graceful degradation for unresolved references

### 2.2 LSP Mode Implementation (0/4 Complete)
- [ ] **Create LSP compilation mode in Slang**
  - [ ] Add `CompilationFlags::LspMode` flag
  - [ ] Implement logic to preserve uninstantiated module symbols
  - [ ] Ensure AST visitor compatibility
  
- [ ] **Modify symbol resolution behavior**
  - [ ] Allow unresolved module instantiations (don't fail compilation)
  - [ ] Generate placeholder symbols for missing modules
  - [ ] Maintain symbol linking for available definitions

- [ ] **Test LSP mode extensively**
  - [ ] Verify no segfaults with incomplete codebases
  - [ ] Validate symbol indexing works with missing dependencies
  - [ ] Performance test on large workspaces

- [ ] **Integration with slangd**
  - [ ] Update `DocumentManager::ParseWithCompilation` to use LSP mode
  - [ ] Verify `SymbolIndex::FromCompilation` works correctly
  - [ ] Test end-to-end LSP features (definitions, symbols, completion)

## Phase 3: Integration & Polish (0% Complete)

### 3.1 Feature Validation (0/5 Complete)
- [ ] **Diagnostics provider** works with auto-discovery
- [ ] **Symbol provider** handles mixed resolved/unresolved code  
- [ ] **Definition provider** gracefully handles missing targets
- [ ] **Completion provider** works with partial compilation
- [ ] **Cross-file navigation** functions correctly

### 3.2 Performance & Scalability (0/3 Complete)
- [ ] **Benchmark startup time** with large workspaces
- [ ] **Memory usage analysis** for auto-discovery vs config mode
- [ ] **Optimization** of file scanning and indexing

### 3.3 Documentation & Examples (0/3 Complete)
- [ ] **Update README** to emphasize zero-config experience
- [ ] **Create examples** showing auto-discovery vs config usage
- [ ] **Migration guide** for existing `.slangd` users

## Current Blockers

### CRITICAL: InstanceSymbol::createInvalid() Crash (2025-09-08)
**Status**: Root cause identified and fixed with targeted solution

**Issue**: slangd crashes with SIGSEGV when processing documents in large workspaces
- **Crash location**: `slang::ast::InterfacePortSymbol::getConnectionAndExpr()`  
- **Actual root cause**: `InstanceSymbol::createInvalid()` tries to elaborate modules with unresolved parameters
- **NOT LINT mode**: LINT mode itself works fine, the issue is specifically in invalid instance creation
- **Impact**: LSP unusable on real-world SystemVerilog codebases with parameterized modules

**Stack Trace** (from debug build):
```
slang::ast::InterfacePortSymbol::getConnectionAndExpr() const
slang::ast::InstanceSymbol::getPortConnection() const  
slang::ast::InstanceSymbol::resolvePortConnections() const
slang::ast::Scope::getCompilation() const
```

**Technical Analysis**:
- SymbolIndex visitor calls `InstanceSymbol::createInvalid()` for modules/interfaces (src/slangd/semantic/symbol_index.cpp:301)
- `createInvalid()` attempts to elaborate modules even when parameters are unresolved
- LSP single-file analysis means parameters are often unavailable (normal scenario)
- Parameter resolution crashes when trying to resolve missing values
- **LINT mode is NOT the problem** - it's the invalid instance creation logic

**Root Cause**: Architectural mismatch between Slang's full-compilation model and LSP's single-file analysis needs

**Solution Implemented**: Skip `InstanceSymbol::createInvalid()` calls in symbol visitor
- Avoids parameter resolution crashes while maintaining most LSP functionality
- Keeps all other symbol indexing working (packages, definitions, regular instances)
- **Status**: WORKING - LSP functional without crashes

**Key Insight**: LSP needs symbol definitions, not full module elaboration. The crash occurs when Slang tries to elaborate modules with unresolved parameters (normal in single-file LSP context).

## Next Immediate Actions
1. ~~**Set up Slang submodule**~~ **COMPLETED**: Using separate repository approach instead  
2. ~~**Fix crash issue**~~ **COMPLETED**: Targeted fix implemented, LSP working without crashes
3. **Validate solution completeness** - Test all LSP features with the current fix
4. **Consider next phase** - Determine if current solution is sufficient or if LSP mode still needed

## Notes & Decisions
- **ConfigManager strategy**: ~~Make optional~~ **REVISED**: Keep always present, don't change file discovery logic (already has auto-discovery fallback), focus only on includes/defines
- **File discovery**: Current `GetSourceFiles()` already auto-discovers when no .slangd - keep unchanged
- **Slang integration**: ~~Use git submodule~~ **CHANGED**: Use separate repository approach via git_override in MODULE.bazel due to Bazel bzlmod incompatibility with git submodules
- **Testing approach**: Create representative mixed codebase for validation
- **Build verification**: Both local builds (`bazel build //...`, `bazel test //...`) and CI builds confirmed working
- **Debug infrastructure**: Added stack trace handler to main.cpp for crash debugging
- **Root cause confirmed**: `InstanceSymbol::createInvalid()` + unresolved parameters, not LINT mode or config file issues
- **Targeted solution**: Skip invalid instance creation in symbol visitor while preserving other functionality

---
*This document tracks implementation progress against the plan outlined in `REFACTOR-PLAN.local.md`*