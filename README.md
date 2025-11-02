# Slangd - SystemVerilog Language Server

[![Build and Test](https://github.com/hankhsu1996/slangd/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/hankhsu1996/slangd/actions/workflows/build.yml)
[![Code Style Check](https://github.com/hankhsu1996/slangd/actions/workflows/style.yml/badge.svg?branch=main)](https://github.com/hankhsu1996/slangd/actions/workflows/style.yml)

Slangd is a SystemVerilog language server implementing the [Language Server Protocol](https://microsoft.github.io/language-server-protocol/). Built on the [Slang](https://github.com/MikePopoloski/slang) compiler frontend, it provides accurate language intelligence for SystemVerilog projects.

## Features

- **Diagnostics** - Real-time syntax and semantic error detection
- **Go-to-definition** - Navigate to symbol definitions across files
- **Document symbols** - Outline view for modules, classes, packages, and more
- **Workspace indexing** - Fast cross-file navigation and symbol resolution
- **Flexible configuration** - Auto-discovery with directory skipping, explicit file lists, path filtering, and more

## Development

### Project Goals

- Build a SystemVerilog Language Server (`slangd`) using Slang and C++23
- Provide a reusable, language-agnostic LSP core library (`lsp`)
- Implement clean asynchronous LSP communication using `jsonrpc` and `asio`
- Follow modern C++ design practices for robustness and maintainability

### Requirements

- Bazel 7.0+ for bzlmod support
- Clang 20+ for C++23 support

### Building and Testing

Build the project:

```bash
bazel build //...
```

Run tests:

```bash
bazel test //...
```

Generate `compile_commands.json` for IDE integration (optional):

```bash
bazel run @hedron_compile_commands//:refresh_all
```

## License

MIT - see [LICENSE](LICENSE) file.
