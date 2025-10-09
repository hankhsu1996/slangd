# Compilation Options

## Overview

This document describes all Slang compilation flags and preprocessor options customized for LSP operation. Understanding these settings is essential for maintaining consistent behavior across the codebase.

## Summary

**Flags we use:**

- `LanguageServerMode` - Enables single-file LSP analysis with auto-instantiation
- `errorLimit = 0` - Report all diagnostics to users

**Flags we avoid:**

- `LintMode` - Suppresses diagnostics in generate blocks

## Locations

These options are configured in three places:

- **OverlaySession** (`overlay_session.cpp`): Per-file compilation with current buffer
- **GlobalCatalog** (`global_catalog.cpp`): Project-wide compilation for metadata extraction
- **Test Fixtures** (`simple_fixture.cpp`): Unit test compilation

**CRITICAL REQUIREMENT:** All three locations MUST use identical options to ensure:

- Tests accurately reflect production behavior
- No surprises between test and production environments
- Consistent diagnostics across all compilation contexts

When adding new options to production code, always update test fixtures to match.

## CompilationOptions

### LintMode (NOT USED)

**We do NOT use LintMode in slangd.**

**What LintMode does:**

- Marks all scopes as uninstantiated, which suppresses diagnostics in generate blocks
- Suppresses "NoTopModules" and "UnknownPackage" diagnostics

**Why we avoid it:**

- Suppressing generate block diagnostics is unacceptable for LSP where users need to see all errors
- Unknown package errors should be reported to users
- LanguageServerMode already handles single-file analysis without these trade-offs

### LanguageServerMode

```cpp
comp_options.flags |= slang::ast::CompilationFlags::LanguageServerMode;
```

**Rationale:** Custom flag added to Slang fork to enable single-file LSP analysis.

**Effect:**

- Uses `UninstantiatedDefSymbol` for module/program instances to avoid full elaboration of sub-modules
- Exception: Interfaces require full elaboration for signal/modport access
- Calls `connectDefaultIfacePorts()` to auto-instantiate interfaces for port connections
- Skips timescale consistency checks not relevant for single-file analysis

**Critical for:** Interface port members to be accessible without top-level instantiation context.

### errorLimit

```cpp
comp_options.errorLimit = 0;  // Unlimited errors
```

**Rationale:** Users need to see all diagnostics in their code through LSP. Unlike command-line compilation where failing fast makes sense, LSP should report every issue.

**Effect:**

- No limit on number of errors reported
- All diagnostics visible in editor
- Prevents hidden errors that would surprise users

## PreprocessorOptions

### initialDefaultNetType

```cpp
pp_options.initialDefaultNetType = slang::parsing::TokenKind::Unknown;
```

**Rationale:** Disable implicit net declarations to catch common bugs. This makes slangd **stricter than the SystemVerilog standard** by default.

**Background:**

- SystemVerilog allows implicit `wire` declarations for undefined LHS identifiers in continuous assignments
- This is legacy Verilog-95 behavior
- Common source of bugs: typos create unintended nets instead of errors

**Example Bug Caught:**

```systemverilog
logic assignment;
assign asignment = foo;  // Typo creates wire 'asignment' instead of error
```

With `initialDefaultNetType = TokenKind::Unknown`, this produces:

```
error: use of undeclared identifier 'asignment'
```

**LSP Philosophy:** Language servers should be stricter than compilers to help developers catch mistakes early (similar to clangd with clang-tidy checks vs. clang).

**User Override:** If working with legacy code that relies on implicit nets:

```systemverilog
`default_nettype wire  // At top of file
```

**Internal Representation:**

- `TokenKind::Unknown` means "no default net type" (equivalent to `` `default_nettype none``)
- `TokenKind::WireKeyword` is the SystemVerilog standard default

## Important: Avoid Full Design Elaboration

**CRITICAL:** Do NOT call these functions in slangd:

- `compilation.getRoot()` - Calls `elaborate()` which does full design elaboration
- `compilation.getAllDiagnostics()` - Calls `getSemanticDiagnostics()` which calls `elaborate()`
- `compilation.getSemanticDiagnostics()` - Directly calls `elaborate()`

**Why we avoid them:**

slangd uses **file-scoped elaboration** to control what gets elaborated. We only traverse module/package/interface bodies from the current file via `SemanticIndex::FromCompilation()`.

Full design elaboration has these problems:

- Elaborates modules from other files unintentionally
- Requires designating a top module (LSP has no concept of "top")
- Performance issues from elaborating the entire design
- Unpredictable behavior when files are incomplete

**Use instead:**

- `compilation.getCollectedDiagnostics()` - Gets diagnostics from diagMap without elaboration
- `compilation.forceElaborate(symbol)` - Targeted file-scoped elaboration
- `SemanticIndex::FromCompilation()` - Our controlled file-scoped traversal

## Future Additions

As we add more customizations, document them here with:

- Option name and value
- Rationale for the choice
- Effect on compilation behavior
- Any user-facing implications

Potential future options:

- `RelaxEnumConversions` for stricter type checking
- Custom warning levels
- Additional LSP-specific flags
