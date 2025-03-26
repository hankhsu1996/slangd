# Slangd - SystemVerilog Language Server

[![Build and Test](https://github.com/hankhsu1996/slangd/actions/workflows/build.yml/badge.svg)](https://github.com/hankhsu1996/slangd/actions/workflows/build.yml)
[![Code Style Check](https://github.com/hankhsu1996/slangd/actions/workflows/style.yml/badge.svg)](https://github.com/hankhsu1996/slangd/actions/workflows/style.yml)

Slangd is a high-performance SystemVerilog language server that implements the [Language Server Protocol (LSP)](https://microsoft.github.io/language-server-protocol/) to bring modern IDE features to hardware description language development. Built on the [Slang](https://github.com/MikePopoloski/slang) frontend, Slangd leverages Slang's impressive speed and extensive SystemVerilog compliance, providing robust language support for large codebases. The project is implemented using modern C++ practices and design patterns for maintainability, safety, and performance.

> ⚠️ **Development Status**: This project is under active development. Features are incomplete, APIs are unstable, and rapid changes should be expected.

## Project Goals

- Provide a Language Server Protocol (LSP) implementation for SystemVerilog using Slang
- Demonstrate modern C++ practices and design patterns
- Implement a complete LSP feature set for SystemVerilog development
- Create a robust and maintainable codebase using C++23 features

## Requirements

- Bazel 7.0+ for bzlmod support
- Clang 19+ for C++23 feature compatibility with libstdc++

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

## Features

Currently focusing on single-file language features powered by the Slang library:

- [x] Document symbols
- [x] Basic diagnostics
- [ ] Hover information
- [ ] Go to definition
- [ ] Find references
- [ ] Code completion

Workspace-wide features will be implemented in future iterations.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
