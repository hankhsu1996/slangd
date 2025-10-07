# Compilation Options

## Overview

This document describes all Slang compilation flags and preprocessor options customized for LSP operation. Understanding these settings is essential for maintaining consistent behavior across the codebase.

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

### LintMode

```cpp
comp_options.flags |= slang::ast::CompilationFlags::LintMode;
```

**Rationale:** Suppresses errors that require a fully elaborated design with a designated top module. Essential for single-file LSP analysis where files are compiled independently.

**Effect:**
- Allows files without top modules to compile
- Suppresses certain cross-module validation errors
- Enables analysis of incomplete designs

### LanguageServerMode

```cpp
comp_options.flags |= slang::ast::CompilationFlags::LanguageServerMode;
```

**Rationale:** Enables LSP-specific behavior in Slang's compilation process.

**Effect:**
- Auto-instantiates interfaces for port resolution (even in non-top modules)
- Treats all module instantiations as `UninstantiatedDefSymbol` (except interfaces)
- Allows proper symbol resolution in single-file mode

**Critical for:** Interface port members to be accessible without full design elaboration.

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
