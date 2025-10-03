# Slangd - SystemVerilog Language Server

[![Build and Test](https://github.com/hankhsu1996/slangd/actions/workflows/build.yml/badge.svg)](https://github.com/hankhsu1996/slangd/actions/workflows/build.yml)
[![Code Style Check](https://github.com/hankhsu1996/slangd/actions/workflows/style.yml/badge.svg)](https://github.com/hankhsu1996/slangd/actions/workflows/style.yml)

Slangd is a high-performance SystemVerilog language server that implements the [Language Server Protocol (LSP)](https://microsoft.github.io/language-server-protocol/) to bring modern IDE features to hardware description language development. Built on the [Slang](https://github.com/MikePopoloski/slang) frontend, Slangd leverages Slang's impressive speed and extensive SystemVerilog compliance, providing robust language support for large codebases. The project is implemented using modern C++ practices and design patterns for maintainability, safety, and performance.

> ⚠️ **Development Status**: This project is under active development. Features are incomplete, APIs are unstable, and rapid changes should be expected.

## Project Goals

- Build a SystemVerilog Language Server (`slangd`) using Slang and C++23
- Provide a reusable, language-agnostic LSP core library (`lsp`)
- Implement clean asynchronous LSP communication using `jsonrpc` and `asio`
- Follow modern C++ design practices for robustness and maintainability

## Requirements

- Bazel 7.0+ for bzlmod support
- Clang 20+ with libc++ 20+ for full C++23 standard library support

## Building and Testing

Build the project:

```
bazel build //...
```

Run tests:

```
bazel test //...
```

Generate `compile_commands.json` for IDE integration (optional):

```
bazel run @hedron_compile_commands//:refresh_all
```

## Feature Coverage

This project is under active development. While some LSP features are implemented, support is currently **limited in scope** and focused on SystemVerilog's **packages and modules** within **single files**.

Notes:

- ✅ **Diagnostics** are available for syntax and semantic errors, but only within a single file. Workspace-wide diagnostics are not yet implemented.
- ✅ **Document symbols** are extracted for top-level constructs like modules and packages. Other constructs (e.g., classes, interfaces, parameters) may be missing.
- 🔜 **Go to definition**, **hover**, and **find references** are planned, and will leverage the existing repository index.
- ⚙️ A **repository-wide indexer** exists and will support upcoming features like cross-file navigation and workspace symbol search. Currently not exposed via LSP.


## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
