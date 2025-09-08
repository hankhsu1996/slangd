# Slangd Refactor Plan: Config-Driven to Auto-Discovery

## Background & Problem Statement

### Current Issues
- **Config dependency problem**: Language server requires `.slangd` config files with top modules and filelists to function
- **User experience**: Users want "open workspace and it works" like other LSP servers (clangd, TypeScript, etc.)
- **Mixed codebase challenge**: Projects have design (Slang-clean) and verification (not Slang-clean) code - current approach forces config-based file filtering
- **LSP best practices**: Modern language servers default to auto-discovery with optional configuration

### Goal
Transform slangd from **config-required** to **config-optional** ("zero-config by default") while maintaining functionality.

## Architectural Analysis

### Current Flow
```
LSP Server → ConfigManager (required) → {DocumentManager, WorkspaceManager} → Feature Providers
```

### Target Flow  
```
LSP Server → Auto-discovery + ConfigManager (optional hints) → Managers → Feature Providers
```

## Implementation Options Analysis

### Option A: Big Bang vs Gradual Migration

**Big Bang Approach** ✅ **CHOSEN**
- **Pros**: Clean result, MVP-focused, no dual complexity, leverage early-stage flexibility
- **Cons**: Higher short-term risk
- **Decision rationale**: Early stage project, no production users, speed matters for MVP

**Gradual Migration** ❌ **REJECTED**  
- **Pros**: Lower risk
- **Cons**: Temporary complexity, slower delivery, maintaining two systems

### Option B: Slang Integration Strategy

**Option 1: Fork Slang + LSP Mode** ✅ **CHOSEN FOR BEST-IN-CLASS**
- **Pros**: 
  - 90% functionality already working
  - Complete SystemVerilog specification coverage
  - Battle-tested by enterprise users
  - One-time learning investment in Slang internals
  - Contribute back to SV ecosystem
- **Cons**: 
  - Learning curve for Slang internals
  - Fork maintenance overhead
- **Decision rationale**: For best possible language server, leverage decades of compiler expertise

**Option 2: Syntax-Tree Only** ❌ **REJECTED FOR FINAL PRODUCT**
- **Pros**: Full control, incremental development
- **Cons**: Reimplementing SystemVerilog semantics = infinite scope, guaranteed bugs
- **Note**: Could be viable for MVP but not sustainable long-term

**Option 3: Keep Current Approach** ❌ **REJECTED**
- **Issue**: LintMode + AST visitor = broken symbol indexing
- **Root cause**: Slang's visitor not designed for partial compilation
- **Result**: Missing symbols, segfaults, incomplete linking

## SystemVerilog vs C++ Complexity

### C++ Model (Simple)
```cpp
#include "path/to/file.h"  // Explicit file path
// Every symbol from explicit includes
// Self-contained compilation units
```

### SystemVerilog Model (Complex)  
```systemverilog
import package_name::*;    // No path - global namespace!
my_module inst();          // No import needed - global namespace!
```

**Key insight**: SystemVerilog requires **global symbol discovery** - can't do single-file compilation like C++.

**Solution**: Two-phase approach
1. **Phase 1**: Parse all `.sv` files, extract global symbol table
2. **Phase 2**: Per-file analysis using global symbol context

## Current Technical Challenges

### Core Issue: LintMode vs Visitor Mismatch
```cpp
// DocumentManager uses LintMode to avoid compilation failures
comp_options.flags |= slang::ast::CompilationFlags::LintMode;

// But SymbolIndex::FromCompilation expects full AST
// Result: Missing submodule instances → visitor crashes/incomplete
```

### Specific Problems
- Submodule instantiations ignored in LintMode
- Port bindings never initialized  
- AST visitor segfaults on incomplete trees
- Symbol linking breaks for unresolved modules

## Configuration Strategy: clangd Model

**Learning from clangd** (C++ and SystemVerilog are both compiled languages):
- ✅ **Auto-discover**: Source files (`.cpp`, `.h` → `.sv`, `.v`)
- ❌ **Require config**: Include directories and defines (via `.clangd`, `compile_commands.json` → `.slangd`)

**Refined approach**:
- **Remove (auto-discover)**: File lists, top modules
- **Keep (explicit config)**: Include directories (`includeDirs`), preprocessor defines  
- **Rationale**: File discovery = easy automation; include paths/defines = project-specific, hard to guess

## Implementation Plan

### Phase 1: Remove ConfigManager Dependencies
1. Modify `SlangdLspServer::OnInitialize()` - remove required ConfigManager creation
2. Update `DocumentManager` constructor - make ConfigManager optional
3. Update `WorkspaceManager` constructor - make ConfigManager optional  
4. Implement fallback auto-discovery logic (already exists in fallback paths)

### Phase 2: Add LSP Mode to Slang
1. **Study Slang internals**: Understand compilation modes, visitor patterns
2. **Identify patch points**: Where LintMode breaks symbol indexing
3. **Implement LSP mode**: Partial compilation that works with AST visitors
   - Allow unresolved symbol references (don't fail compilation)
   - Generate AST nodes for uninstantiated modules (enable visitor traversal)
   - Preserve symbol linking even with missing dependencies
4. **Test extensively**: Ensure no regressions in existing functionality

### Phase 3: Integration & Testing
1. Update `SymbolIndex::FromCompilation` to use LSP mode
2. Comprehensive testing with mixed design/verification codebases
3. Performance validation on large projects
4. Update documentation and examples

## Decision Record

**Date**: 2025-09-08  
**Decision**: Proceed with **Big Bang refactor** using **Fork Slang + LSP Mode** approach  
**Participants**: Project maintainer + Claude analysis  
**Rationale**: 
- Early stage project allows for breaking changes
- MVP speed is priority  
- Best long-term approach for production-quality language server
- Leverage existing Slang expertise rather than reimplementing SystemVerilog semantics

## Next Steps
1. Create detailed technical analysis of Slang compilation modes
2. Identify specific patch requirements for LSP mode
3. Set up Slang fork and development environment  
4. Begin incremental implementation starting with DocumentManager changes

## References
- Current architecture documented in `CONFIG-ANALYSIS.local.md`
- SystemVerilog specification challenges
- clangd configuration model
- Industry LSP best practices