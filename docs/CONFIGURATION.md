# Configuration

slangd uses a `.slangd` YAML configuration file in the workspace root to control project settings and file discovery.

## Configuration File Location

```
<workspace-root>/.slangd
```

The configuration file is loaded automatically when the workspace is opened. Changes to the configuration file trigger a rebuild of the project layout.

## File Discovery

slangd supports two file discovery modes:

**Explicit file sources** - When `Files` or `FileLists` are specified:
```yaml
Files:
  - rtl/top.sv
  - rtl/design.sv

FileLists:
  Paths:
    - rtl/rtl.f
    - tb/tb.f
  Absolute: false  # Paths in .f files are relative to .f file location
```

**Auto-discovery** - When no file sources are specified, slangd recursively scans the workspace for SystemVerilog files (*.sv, *.svh, *.v, *.vh):
```yaml
# No Files/FileLists specified - auto-discovery mode
IncludeDirs:
  - include/
```

## Path Filtering

Use the `If` block to filter files during discovery. Both explicit file sources and auto-discovered files are filtered.

### PathExclude

Exclude files matching a regex pattern (paths relative to workspace root):

```yaml
If:
  PathExclude: .*/generated/.*    # Exclude generated code
```

Common patterns:
- `.*build/.*` - Exclude build outputs
- `.*/generated/.*` - Exclude generated code
- `.*_tb\.sv` - Exclude testbenches

### PathMatch

Include only files matching a regex pattern:

```yaml
If:
  PathMatch: rtl/.*\.sv$     # Only RTL .sv files
```

### Combined Conditions

PathMatch and PathExclude use AND logic:

```yaml
If:
  PathMatch: rtl/.*          # Must be under rtl/
  PathExclude: .*/generated/.*    # But not in generated/
```

This includes files under `rtl/` except those in `rtl/generated/`.

### Pattern Syntax

- Patterns use C++ regex syntax (not glob)
- Paths are relative to workspace root
- Forward slashes on all platforms (UNIX-style)
- Empty patterns or no `If` block includes everything
- Invalid regex logs warning and includes all files (fail-open)

## Include Directories

Specify search paths for SystemVerilog `include` directives:

```yaml
IncludeDirs:
  - rtl/include/
  - common/defines/
```

## Preprocessor Defines

Define macros for compilation:

```yaml
Defines:
  - DEBUG
  - SIMULATION
  - WIDTH: 32           # Key-value syntax
  - DEPTH: 1024
```

Key-value defines are converted to `NAME=VALUE` format.

## Complete Example

```yaml
# File discovery
FileLists:
  Paths:
    - rtl/rtl.f
  Absolute: false

# Path filtering
If:
  PathMatch: rtl/.*
  PathExclude: .*/generated/.*

# Include paths
IncludeDirs:
  - rtl/include/
  - common/

# Defines
Defines:
  - SIMULATION
  - DATA_WIDTH: 64
```

## Precedence

1. Explicit file sources (`Files`, `FileLists`) are used if present
2. Otherwise, auto-discovery scans the workspace
3. Path filtering (`If` block) applies to both modes
4. Include paths and defines apply to all compilations

## Related Documentation

- `SERVER_ARCHITECTURE.md` - Server design and session management
- `COMPILATION_OPTIONS.md` - Slang compiler flags
