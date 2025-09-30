# CLAUDE.md

This file provides guidance to Claude Code when working with code in this repository.

## Quick Reference

**Build & Test:**

- `bazel build //...` - Build everything
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

**Design Principle:**
Leverage existing Slang library infrastructure rather than overengineering. The best LSP solutions are simple and reuse Slang's existing resolution logic.

**Pre-commit:**

1. Format code, check build/test pass
2. Use `feature/`, `bugfix/`, `refactor/` branch prefixes
3. Commit messages: Short summary + bullet points using '-' in details, no Claude attribution, focus on 'what' changed not process/phase/step

## Architecture

SystemVerilog LSP server with modular design:

- **`lsp/`**: Generic LSP protocol implementation (JSON-RPC, ASIO coroutines)
- **`slangd/`**: SystemVerilog server using Slang library
- **GlobalCatalog + OverlaySession**: 1-5ms response times with cross-file support
- **SemanticIndex**: Unified reference+definition storage with file-scoped traversal optimization (99.8% perf improvement)
  - Single-hop go-to-definition lookup using embedded definition ranges
  - ReferenceEntry structs combine source location + target definition for robust cross-file navigation
- **Current features**: Diagnostics, go-to-definition, document symbols

## Development Tips

**Coding Standards:**

- C++23, ASIO coroutines, `std::expected`, trailing return types
- Use `toString(symbol.kind) -> std::string_view` for Slang enum printing

**General Debugging:**

- We are not using GDB, because it's harder to do batch debugging.
- We use spdlog for logging when needed.
- Do not use `env SPDLOG_LEVEL=debug bazel test //...`. Set the log level in each test file instead.
- Default log level is already set to debug - do not add SPDLOG_LEVEL=xxx to test commands.
- Generally, just do `bazel test //...` even if you are changing a single file, we don't have that mush tests, so it is fast.

**Privacy Requirements:**

- **NEVER** use actual proprietary code examples or variable/signal names from the user's codebase in unit tests
- Always create minimal, generic examples for testing (e.g., use `packet_t`, `counter_t` instead of proprietary names)
- User code examples are confidential and must not be included in the repository

**AST/CST Debugging:**

```bash
# Use debug/ directory (gitignored) for AST investigation
mkdir -p debug
echo 'module test; function logic f(); endfunction; endmodule' > debug/test.sv
slang debug/test.sv --ast-json debug/ast.json
slang debug/test.sv --cst-json debug/cst.json
```

- Keep test files minimal - JSON output is extremely large.
- 'slang' is installed in system.
