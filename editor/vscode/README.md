# SystemVerilog VS Code Extension (Experimental)

This extension provides **early-stage SystemVerilog language support** for Visual Studio Code using the [slangd](https://github.com/hankhsu1996/slangd) language server.

> ⚠️ **Warning**: This extension is still in development. Features are incomplete and the interface may change without notice.

## What works today

- ✅ Basic diagnostics (syntax and semantic errors) via `slangd`
- ✅ Document and workspace-level navigation (symbols, outlines, etc.)
- 🛠️ Built directly on top of the in-progress [slangd](https://github.com/hankhsu1996/slangd) C++ LSP implementation

## What doesn't (yet)

- ❌ No stable API
- ❌ No support for user settings
- ❌ No guarantee of correctness or stability
- ❌ No language features beyond diagnostics and navigation

Most LSP features such as hover, go-to-definition, and reference search are planned but not yet implemented.

## How to try it

1. Build the `slangd` binary (already bundled in the extension).
2. Install the extension manually (`.vsix`) or from your local build.
3. Open a SystemVerilog (`.sv`) file and check if diagnostics appear.

## Development context

This extension is primarily a testbed for integrating [`slangd`](https://github.com/hankhsu1996/slangd) into editors. It is not intended for general production use at this stage.

## Feedback

Please open issues or suggestions at [slangd GitHub repo](https://github.com/hankhsu1996/slangd/issues).

## License

MIT © 2025 Shou-Li Hsu
