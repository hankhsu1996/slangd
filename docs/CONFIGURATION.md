# Configuration

slangd uses a `.slangd` YAML configuration file in the workspace root to control project settings and file discovery.

## Configuration File Location

```
<workspace-root>/.slangd
```

The configuration file is loaded automatically when the workspace is opened. Changes to the configuration file trigger a rebuild of the project layout.

## File Discovery

slangd discovers files through three sources that can be combined:

### Workspace Auto-Discovery

Use `AutoDiscover` to enable/disable workspace scanning (default: enabled):

```yaml
AutoDiscover: true  # Recursively scan workspace for *.sv, *.svh, *.v, *.vh
```

When enabled, slangd scans the entire workspace directory for SystemVerilog files. This is useful for projects without explicit file lists.

### Explicit File Sources

Specify individual files or filelist files:

```yaml
# Individual source files
Files:
  - rtl/top.sv
  - /external/uvm-1.2/src/uvm_pkg.sv  # External dependencies

# Filelist files (.f format)
FileLists:
  Paths:
    - rtl/rtl.f
    - tb/tb.f
  Absolute: false  # Paths in .f files are relative to .f file location
```

### Additive Behavior

All file sources are **additive** - you can combine auto-discovery with explicit files:

```yaml
AutoDiscover: true       # Scan workspace
Files:
  - /vcs/uvm-1.2/src/uvm_pkg.sv  # Add external UVM package
IncludeDirs:
  - /vcs/uvm-1.2/src
```

This discovers all workspace files plus the external UVM package, which is the recommended pattern for adding external dependencies like UVM.

To use only explicit files without workspace scanning:

```yaml
AutoDiscover: false
FileLists:
  Paths: [rtl/rtl.f]
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

## Complete Examples

### RTL-only Project with Explicit Control

```yaml
# Disable auto-discovery, use only filelist
AutoDiscover: false
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

### Verification Project with UVM

```yaml
# Auto-discover workspace + add external UVM
AutoDiscover: true
Files:
  - /tools/vcs/2023.12/etc/uvm-1.2/src/uvm_pkg.sv
IncludeDirs:
  - /tools/vcs/2023.12/etc/uvm-1.2/src
  - rtl/include/
If:
  PathMatch: (rtl|tb)/.*
  PathExclude: .*/generated/.*
Defines:
  - UVM_NO_DPI
  - SIMULATION
```

## Precedence

1. **File Discovery**: All sources are additive
   - If `AutoDiscover: true` (default), workspace files are discovered
   - Files from `FileLists` are added
   - Files from `Files` are added
   - Duplicates are automatically removed
2. **Path Filtering**: `If` block filters all discovered files
3. **Compilation Settings**: `IncludeDirs` and `Defines` apply to all files

## Related Documentation

- `SERVER_ARCHITECTURE.md` - Server design and session management
- `COMPILATION_OPTIONS.md` - Slang compiler flags
