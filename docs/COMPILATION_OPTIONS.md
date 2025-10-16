# Compilation Options

## Overview

This document describes all Slang compilation flags and preprocessor options used in slangd. All options are configured identically in three locations to ensure consistent behavior.

## Quick Reference

**Options we use:**

- `LanguageServerMode` - Single-file LSP analysis with auto-instantiation
- `errorLimit = 0` - Report all diagnostics (no limit)
- `enableLegacyProtect = true` - Support legacy `` `protected`` directives
- `initialDefaultNetType = Unknown` - Disable implicit net declarations (stricter than standard)

**Options we avoid:**

- `LintMode` - Suppresses diagnostics in generate blocks

## Implementation Locations

When adding/modifying options, update all three locations:

1. `src/slangd/services/overlay_session.cpp` - Per-file compilation
2. `src/slangd/services/preamble_manager.cpp` - Project-wide compilation
3. `test/slangd/common/semantic_fixture.hpp` - Test fixtures

**CRITICAL:** All three must use identical options. Tests that don't match production behavior are worse than no tests.

## CompilationOptions

### LanguageServerMode

```cpp
comp_options.flags |= slang::ast::CompilationFlags::LanguageServerMode;
```

**Purpose:** Enable single-file LSP analysis without requiring top-level instantiation.

**What it does:**

- Uses `UninstantiatedDefSymbol` for module/program instances (avoids full elaboration)
- Exception: Interfaces are fully elaborated for signal/modport access
- Calls `connectDefaultIfacePorts()` for interface port connections
- Skips timescale consistency checks

**Why:** Interface members must be accessible without top-level context for LSP features to work.

### errorLimit

```cpp
comp_options.errorLimit = 0;  // Unlimited
```

**Purpose:** Show all diagnostics to users.

**Why:** Unlike CLI tools that fail fast, LSP should report every issue. Users need complete visibility.

### LintMode (NOT USED)

We explicitly do NOT use `LintMode` because it:

- Marks all scopes as uninstantiated → suppresses diagnostics in generate blocks
- Suppresses "NoTopModules" and "UnknownPackage" diagnostics

LSP users need to see all errors. `LanguageServerMode` provides single-file analysis without these drawbacks.

## LexerOptions

### enableLegacyProtect

```cpp
lexer_options.enableLegacyProtect = true;
```

**Purpose:** Support legacy protected IP directives (`` `protect``, `` `protected``, `` `endprotect``, `` `endprotected``).

**What it does:**

- Recognizes legacy protection directives from older EDA tools (Verilog-XL, Verilog-A)
- Skips protected regions during preprocessing
- Emits `ProtectedEnvelope` diagnostics when encountered

**Why:** Many codebases include third-party IP with legacy encryption. Compatibility is essential for LSP usefulness.

## PreprocessorOptions

### initialDefaultNetType

```cpp
pp_options.initialDefaultNetType = slang::parsing::TokenKind::Unknown;
```

**Purpose:** Disable implicit net declarations (stricter than SystemVerilog standard).

**What it does:** Treats undefined identifiers in continuous assignments as errors instead of auto-creating `wire` declarations.

**Why:** Implicit nets are a common source of bugs (typos silently create unintended nets). LSP should catch mistakes early.

**Example:** `assign asignment = foo;` → error instead of creating wire `asignment`.

**User override:** Legacy code can use `` `default_nettype wire`` at file top.

## Critical: Avoid Full Design Elaboration

**DO NOT call:**

- `compilation.getRoot()` - Triggers full elaboration
- `compilation.getAllDiagnostics()` - Triggers full elaboration
- `compilation.getSemanticDiagnostics()` - Triggers full elaboration

**Why:** slangd uses file-scoped elaboration via `SemanticIndex::FromCompilation()`. Full elaboration:

- Elaborates unrelated files
- Requires a top module (LSP has no "top")
- Causes performance issues
- Behaves unpredictably on incomplete files

**Use instead:**

- `compilation.getCollectedDiagnostics()` - Gets diagnostics without elaboration
- `compilation.forceElaborate(symbol)` - Targeted elaboration
- `SemanticIndex::FromCompilation()` - Controlled file-scoped traversal
