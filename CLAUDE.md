# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build and Test Commands

- **Build everything**: `bazel build //...`
- **Run all tests**: `bazel test //...`
- **Build with configuration**: `bazel build //... --config=debug` (or `release`, `fastbuild`)
- **Run specific test**: `bazel test //test/slangd:document_manager_test`
- **Generate compile_commands.json**: `bazel run @hedron_compile_commands//:refresh_all`

## Requirements

- Bazel 7.0+ with bzlmod support
- Clang 19+ for C++23 features

## Architecture

This is a SystemVerilog Language Server Protocol (LSP) implementation with a modular design separating generic LSP functionality from language-specific features.

### Core Components

**LSP Core Library (`lsp/`)**

- Language-agnostic LSP protocol implementation built on JSON-RPC
- Base `LspServer` class with virtual handlers for LSP lifecycle and features
- Document management and ASIO coroutine-based async operations

**Slangd Server (`slangd/`)**

- SystemVerilog LSP server extending the generic `LspServer`
- Uses Slang library for SystemVerilog parsing and semantic analysis
- Core managers handle configuration, documents, and workspace operations
- Feature providers implement specific LSP capabilities (diagnostics, symbols, definitions)

### Key Managers

- **ConfigManager**: Handles slangd configuration files
- **DocumentManager**: Manages open documents and their Slang compilation units
- **WorkspaceManager**: Workspace-level file operations and indexing
- **SymbolIndex**: Repository-wide symbol indexing for cross-file navigation

### Dependencies

- **Slang**: SystemVerilog compiler frontend for parsing and semantics
- **jsonrpc**: Custom JSON-RPC library with ASIO integration
- **ASIO**: Asynchronous I/O with C++20 coroutines
- **spdlog**: Structured logging
- **yaml-cpp**: Configuration file parsing

### Coding Standards

Follow Google C++ Style Guide with these specifics:

- Use C++23 features, avoid macros
- ASIO coroutines with strands for synchronization (no mutexes/futures)
- `std::expected` for error handling over exceptions
- Trailing return types (`auto Foo() -> Type`) for clarity
- Smart pointers and RAII for memory management

### Current LSP Features

- Diagnostics for syntax/semantic errors (single file scope)
- Document symbols for SystemVerilog modules and packages
- Basic go-to-definition support
- Repository-wide symbol indexing (infrastructure complete, not yet exposed via LSP)

The architecture enables future language servers (e.g. VHDL) to reuse the generic `lsp` core.
