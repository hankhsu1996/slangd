# Slangd - SystemVerilog Language Server

[![Build and Test](https://github.com/hankhsu1996/slangd/actions/workflows/build.yml/badge.svg)](https://github.com/hankhsu1996/slangd/actions/workflows/build.yml)
[![Code Style Check](https://github.com/hankhsu1996/slangd/actions/workflows/style.yml/badge.svg)](https://github.com/hankhsu1996/slangd/actions/workflows/style.yml)

Slangd is a high-performance SystemVerilog language server that implements the [Language Server Protocol (LSP)](https://microsoft.github.io/language-server-protocol/) to bring modern IDE features to hardware description language development. Built on the [Slang](https://github.com/MikePopoloski/slang) frontend, Slangd leverages Slang's impressive speed and extensive SystemVerilog compliance, providing robust language support for large codebases. The project is implemented using modern C++ practices and design patterns for maintainability, safety, and performance.

> ⚠️ **Development Status**: This project is under active development. Features are incomplete, APIs are unstable, and rapid changes should be expected.

## Project Goals

- Provide a complete LSP protocol implementation for SystemVerilog
- Support all major SystemVerilog language features
- Deliver robust error handling and diagnostics
- Optimize performance for large hardware design projects
- Integrate seamlessly with modern IDEs like VS Code

## Requirements

- Bazel 7.0+ for bzlmod support
- Clang 19+ for C++23 features

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

Current implementation status:

### Language Server Features

Slangd implements the Language Server Protocol for SystemVerilog:

- [x] Document synchronization (open, change, close)
- [ ] Diagnostics reporting
- [ ] Document symbols (in progress)
- [ ] Workspace symbols
- [ ] Hover information
- [ ] Go to definition
- [ ] Find references
- [ ] Code completion
- [ ] Formatting
- [ ] Semantic highlighting

### Implementation Details

- [x] Built on Slang parser and compiler
- [x] Slang library integrated with Bazel build system
- [x] Complete JSON-RPC library implementation
- [x] Transport layer implementation (pipe, socket, stdio)
- [x] Asynchronous operation with ASIO coroutines
- [x] Extensive test suite

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
