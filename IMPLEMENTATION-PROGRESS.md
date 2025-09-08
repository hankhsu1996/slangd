# Implementation Progress: Config-Driven to Auto-Discovery Refactor

## Status Overview
- **Started**: 2025-09-08
- **Current Phase**: Planning & Setup
- **Overall Progress**: 0% (Planning complete, implementation not started)

## Phase 1: Setup & ConfigManager Refactor (0% Complete)

### 1.1 Slang Integration Setup
- [ ] **Convert Slang to git submodule** (Current: Bazel external dependency)
  - [ ] Add Slang as git submodule
  - [ ] Update Bazel BUILD files to use local Slang
  - [ ] Verify build still works
  - [ ] Document submodule workflow

### 1.2 Make ConfigManager Optional (0/4 Complete)
- [ ] **SlangdLspServer changes** (`src/slangd/core/slangd_lsp_server.cpp:40-50`)
  - [ ] Modify `OnInitialize()` to conditionally create ConfigManager
  - [ ] Check for `.slangd` file existence before creating ConfigManager
  - [ ] Pass nullable ConfigManager to other managers
  
- [ ] **DocumentManager changes** (`src/slangd/core/document_manager.cpp:24`)
  - [ ] Make ConfigManager parameter optional in constructor
  - [ ] Add fallback logic for defines/includeDirs when ConfigManager is null
  - [ ] Update `ParseWithCompilation()` to handle null config
  
- [ ] **WorkspaceManager changes** (`src/slangd/core/workspace_manager.cpp`)
  - [ ] Make ConfigManager parameter optional in constructor  
  - [ ] Implement auto-discovery file scanning (may already exist as fallback)
  - [ ] Ensure workspace scanning works without config
  
- [ ] **Update all manager constructors**
  - [ ] Change ConfigManager from `std::shared_ptr<ConfigManager>` to `std::shared_ptr<ConfigManager> = nullptr`
  - [ ] Update all call sites

### 1.3 Test Basic Auto-Discovery (0/3 Complete)
- [ ] **Create test workspace** without `.slangd` file
- [ ] **Verify basic functionality** works (file discovery, parsing, basic diagnostics)
- [ ] **Document any issues** found with auto-discovery mode

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
- None (in planning phase)

## Next Immediate Actions
1. **Set up Slang submodule** - enables deep analysis of internals
2. **Create simple test case** - workspace with mixed design/verification code to validate approach
3. **Begin ConfigManager refactor** - start with making it optional

## Notes & Decisions
- **ConfigManager strategy**: Make optional, keep for defines/includeDirs (don't remove entirely)
- **Slang integration**: Use git submodule for easier analysis and patching
- **Testing approach**: Create representative mixed codebase for validation

---
*This document tracks implementation progress against the plan outlined in `REFACTOR-PLAN.local.md`*