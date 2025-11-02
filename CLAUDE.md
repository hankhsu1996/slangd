# CLAUDE.md

This file provides guidance to Claude Code when working with code in this repository.

---

## CRITICAL: PRIVACY REQUIREMENTS

**NEVER use actual proprietary names from user's codebase in:**

- Tests, code, documentation, commit messages, or PR descriptions
- **ALWAYS use generic placeholders only**

**Before commit/PR: verify no proprietary names anywhere**

---

## Quick Reference

**Build & Test:**

- `bazel build //...` - Build everything
- `bazel build //... --config=debug` - Build with debug symbols
- `bazel test //...` - Run all tests
- `find src include test -name "*.cpp" -o -name "*.hpp" | xargs clang-format -i` - Format code
- `bazel run @hedron_compile_commands//:refresh_all` - Refresh compile database

**TDD for new SV features:**

1. Write failing test first
2. Add debug printing to understand AST structure
3. Implement minimal fix
4. Clean up and remove debug code

- **Unit test files**: <300 lines total
- **Individual test cases**: <50 lines each

**Adding new language features:**

When adding support for new SystemVerilog constructs:

1. Add tests first (TDD approach)
2. Check `docs/SEMANTIC_INDEXING.md` for implementation patterns
3. **Check existing Slang infrastructure** - look for `get*()`, `find()`, or resolution methods before building custom solutions
4. Add definition extraction + handlers as needed
5. Verify with both self-definition and reference tests
6. **CRITICAL: Preamble symbol coverage** - If symbols can be referenced from packages, ensure `PreambleSymbolVisitor` indexes them. Missing symbols from `symbol_info_` map cause segfaults or invalid coordinates. See `docs/PREAMBLE.md` for details.

**Design Principle:**
Leverage existing Slang library infrastructure rather than overengineering. The best LSP solutions are simple and reuse Slang's existing resolution logic.

## Design Principles (CRITICAL - Read Before Major Changes)

Before implementing LSP features or modifying Slang integration, read `docs/DESIGN_PRINCIPLES.md` for detailed guidance and case studies.

**Quick Checklist for New Features:**

- [ ] Have you checked if Slang already provides this information?
- [ ] Are you following existing patterns in Slang and slangd codebases?
- [ ] Does your solution require manual searching or state tracking? (RED FLAG - rethink approach)
- [ ] Can you explain why the current design exists before changing it?
- [ ] Is the solution simple (typically <50 lines) and maintainable?
- [ ] Does it feel elegant, or does it feel like a workaround?

**If any checklist item fails, STOP and consult `docs/DESIGN_PRINCIPLES.md`**

Key principle: Work with the system's design, not against it. Complexity is often self-inflicted from not understanding what the library already provides.

**Pre-commit:**

1. Format code, check build/test pass
2. Use `feature/`, `bugfix/`, `refactor/` branch prefixes
3. Commit messages: Short summary + bullet points using '-' in details, no Claude attribution, focus on 'what' changed not process/phase/step

## Architecture

SystemVerilog LSP server with modular design:

- **`lsp/`**: Generic LSP protocol implementation (JSON-RPC, ASIO coroutines)
- **`slangd/`**: SystemVerilog server using Slang library
- **SessionManager**: Centralized session lifecycle (create/cache/invalidate). **Features are read-only - never create sessions directly.**
- **PreambleManager + OverlaySession**: 1-5ms response times with cross-file support
- **SemanticIndex**: Unified reference+definition storage with file-scoped traversal optimization (99.8% perf improvement)
  - Single-hop go-to-definition lookup using embedded definition ranges
  - ReferenceEntry structs combine source location + target definition for robust cross-file navigation
- **Current features**: Diagnostics, go-to-definition, document symbols

## Documentation

- **`docs/DESIGN_PRINCIPLES.md`**: Core philosophy and case studies for working with Slang library infrastructure
- **`docs/SERVER_ARCHITECTURE.md`**: Server layers, session lifecycle, two-phase diagnostics, and component overview
- **`docs/ASYNC_ARCHITECTURE.md`**: Detailed async patterns, executor model, channel synchronization, and coroutine coordination
- **`docs/SESSION_MANAGEMENT.md`**: Session lifecycle management, memory-bounded caching, eviction policy, and VSCode interaction patterns
- **`docs/PREAMBLE.md`**: Preamble architecture for packages and interfaces, cross-compilation symbol binding, and critical constraints around missing symbols
- **`docs/CONFIGURATION.md`**: .slangd config file format, file discovery modes, and path filtering
- **`docs/SEMANTIC_INDEXING.md`**: SemanticIndex implementation patterns and guide to adding new symbol support
- **`docs/COMPILATION_OPTIONS.md`**: Slang compilation flags and preprocessor options used for LSP operation
- **`docs/LSP_TYPE_HANDLING.md`**: TypeReferenceSymbol architecture and type traversal deduplication patterns
- **`docs/SYMBOL_CONTEXT_HANDLING.md`**: Expression-driven reference architecture (symbols define, expressions reference)

**Documentation Guidelines:**

- **No emojis**: Never use emojis in documentation files (including warning symbols, checkmarks, X marks)
- **Concise**: Keep architecture docs under 300 lines; focus on critical decisions and constraints
- **Focus**: Document nuances, edge cases, and "why" over "what"
- **Technical accuracy**: Prefer precise technical descriptions over marketing language

## Development Tips

**Requirements:**

- Clang 20+ for C++23 support (configured in `.bazelrc`)
- Uses default libstdc++ standard library
- Ubuntu: `sudo apt install clang-20`

**Coding Standards:**

- C++23, ASIO coroutines, `std::expected`, trailing return types
- Use modern C++23 standard library features (e.g., `std::ranges::contains`)
- Use `toString(symbol.kind) -> std::string_view` for Slang enum printing
- Use `slang::syntax::toString(syntax.kind) -> std::string_view` for SyntaxKind printing
- Print source ranges: `range.start().offset()..range.end().offset()` for offsets, or use `source_manager.getLineNumber(range.start())` for line numbers
- Naming: prefer full words over abbreviations; remove redundant context from names
- Comments: describe technical behavior, not project state (no alpha/beta/v1/staging/milestone)

**Error Handling:**

- **Preconditions** (programmer errors): Fail-fast, no runtime checks
- **Runtime errors** (expected failures): Use `std::expected<T, std::string>`
- **Distinction**: Can this happen in correct code? No → precondition (crash). Yes → std::expected (handle).

**Forward Declaration vs Include:**

- Headers: forward declare pointers/references; include for values/methods/inheritance
- Avoid `auto` for base conversions: `const Symbol& sym = getSymbol()` not `const auto& sym = getSymbol()`

**General Debugging:**

- We are not using GDB, because it's harder to do batch debugging.
- We use spdlog for logging when needed.
- Do not use `env SPDLOG_LEVEL=debug bazel test //...`. Set the log level in each test file instead.
- Default log level is already set to debug - do not add SPDLOG_LEVEL=xxx to test commands.
- Generally, just do `bazel test //...` even if you are changing a single file, we don't have that mush tests, so it is fast.
- For temporary logging, use `spdlog::debug("message")` directly (not instance logger). Remove before committing.

**AST/CST Debugging:**

```bash
# Use debug/ directory (gitignored) for AST investigation
mkdir -p debug
echo 'module test; function logic f(); endfunction; endmodule' > debug/test.sv
slang debug/test.sv --ast-json debug/ast.json
slang debug/test.sv --cst-json debug/cst.json
```

- Keep test files minimal - JSON output is extremely large.
- Use `jq` for efficient JSON querying.
- 'slang' is installed in system.
