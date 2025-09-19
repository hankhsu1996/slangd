# CLAUDE.md

This file provides guidance to Claude Code when working with code in this repository.

## Quick Reference

**Build & Test:**
- `bazel build //...` - Build everything
- `bazel test //...` - Run all tests
- `find src include test -name "*.cpp" -o -name "*.hpp" | xargs clang-format -i` - Format code

**TDD for new SV features:**
1. Write failing test first
2. Add debug printing to understand AST structure
3. Implement minimal fix
4. Clean up and remove debug code

**Pre-commit:**
1. Format code, check build/test pass
2. Use `feature/`, `bugfix/`, `refactor/` branch prefixes
3. Commit messages: Short summary + bullet points using '-' in details, no Claude attribution

## Architecture

SystemVerilog LSP server with modular design:
- **`lsp/`**: Generic LSP protocol implementation (JSON-RPC, ASIO coroutines)
- **`slangd/`**: SystemVerilog server using Slang library
- **GlobalCatalog + OverlaySession**: 1-5ms response times with cross-file support
- **Current features**: Diagnostics, go-to-definition, document symbols

## Development Tips

**Coding Standards:**
- C++23, ASIO coroutines, `std::expected`, trailing return types
- Use `toString(symbol.kind)` for Slang enum printing

**AST Debugging:**
```bash
# Use debug/ directory (gitignored) for AST investigation
echo 'module test; function logic f(); endfunction; endmodule' > debug/test.sv
slang debug/test.sv --ast-json debug/test.json
```
Keep test files minimal - JSON output is extremely large.
