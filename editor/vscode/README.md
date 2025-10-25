# Slangd

SystemVerilog language support for VS Code. Built on [slang](https://github.com/MikePopoloski/slang), the fastest and most compliant open-source SystemVerilog frontend, Slangd uses a full compiler frontend for accurate language understanding, not regex-based approximations.

## Features

- **Real-time diagnostics** from a proven compiler frontend
- **Go-to-definition** across your entire workspace
- **Document symbols** for exploring design hierarchy

## Requirements

- VS Code 1.85.0 or later
- Linux (statically linked, works on any distribution)

The slangd binary is bundled with the extension.

## Configuration

Slangd works out of the box by auto-discovering SystemVerilog files. For more control, create a `.slangd` configuration file.

### Extension Settings

- `systemverilog.server.path` - Custom path to slangd executable (leave empty to use bundled version)
- `systemverilog.server.logLevel` - Server log level: `trace`, `debug`, `info`, `warn`, `error`, `off`

### Workspace Configuration (Optional)

Create a `.slangd` file in your workspace root for advanced project settings:

```yaml
# File discovery (default: auto-discover workspace files)
AutoDiscover: true

# Explicit source files
Files:
  - path/to/file.sv

# Filelist files (.f format)
FileLists:
  Paths:
    - rtl/rtl.f
  Absolute: false

# Path filtering (regex patterns)
If:
  PathMatch: rtl/.*\.sv # Include only matching files
  PathExclude: .*/build/.* # Exclude matching files

# Include directories
IncludeDirs:
  - rtl/include/

# Preprocessor defines
Defines:
  - SIMULATION
  - DATA_WIDTH: 32
```

See [configuration documentation](https://github.com/hankhsu1996/slangd/blob/main/docs/CONFIGURATION.md) for detailed examples and advanced usage.

## Credits

Extension icon: [Web developer icons created by kerismaker - Flaticon](https://www.flaticon.com/free-icons/web-developer)

## License

MIT © 2025 Shou-Li Hsu
