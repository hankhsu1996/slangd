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

#### Selective Directory Discovery

For large workspaces (especially monorepos), you can specify which directories to discover for better performance:

```yaml
AutoDiscover:
  Enabled: true
  DiscoverDirs:
    - rtl                   # Simple directory from workspace root
    - common                # Simple directory from workspace root
    - design/subsystem-a    # Nested path from workspace root
    - design/subsystem-b    # Nested path from workspace root
```

**Discovery Rules:**
- Hidden directories (starting with `.`) are always skipped automatically (e.g., `.git`, `.cache`, `.vscode`)
- `DiscoverDirs` uses path-based matching relative to workspace root
- If `DiscoverDirs` is empty or omitted, the entire workspace is discovered (default behavior)
- If `DiscoverDirs` is specified, only those paths are traversed (significant performance benefit for monorepos)
- The old boolean format (`AutoDiscover: true`) is still supported for backward compatibility

**Example for monorepo:**
```yaml
AutoDiscover:
  Enabled: true
  DiscoverDirs: [rtl, common, design/subsystem-a]
```

This discovers only SystemVerilog files in specified directories, significantly reducing discovery time in large monorepos with mixed languages.

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

Exclude files matching a regex pattern (paths relative to workspace root).

**Single pattern:**
```yaml
If:
  PathExclude: .*/generated/.*    # Exclude generated code
```

**Multiple patterns (OR logic):**
```yaml
If:
  PathExclude:
    - .*/generated/.*     # Exclude generated code OR
    - .*_tb\.sv           # testbenches OR
    - .*/build/.*         # build outputs
```

When multiple patterns are specified, a file is excluded if it matches **any** pattern (OR relationship).

Common patterns:
- `.*build/.*` - Exclude build outputs
- `.*/generated/.*` - Exclude generated code
- `.*_tb\.sv` - Exclude testbenches

### PathMatch

Include only files matching a regex pattern.

**Single pattern:**
```yaml
If:
  PathMatch: rtl/.*\.sv$     # Only RTL .sv files
```

**Multiple patterns (OR logic):**
```yaml
If:
  PathMatch:
    - rtl/.*\.sv      # Include RTL .sv files OR
    - common/.*\.sv   # common .sv files
```

When multiple patterns are specified, a file is included if it matches **any** pattern (OR relationship).

### Combined Conditions

PathMatch and PathExclude use AND logic between them:

**Single patterns:**
```yaml
If:
  PathMatch: rtl/.*          # Must be under rtl/ AND
  PathExclude: .*/generated/.*    # not in generated/
```

**Mixed single and list:**
```yaml
If:
  PathMatch:
    - rtl/.*        # Must match rtl/ OR tb/ (OR within PathMatch) AND
    - tb/.*
  PathExclude: .*/generated/.*    # not match generated/ (single pattern)
```

**Multiple lists:**
```yaml
If:
  PathMatch:
    - rtl/.*        # Must match rtl/ OR common/ (OR within PathMatch) AND
    - common/.*
  PathExclude:
    - .*/generated/.*    # not match generated/ OR build/ (OR within PathExclude)
    - .*/build/.*
```

**Logic summary:**
- Within a condition (PathMatch or PathExclude): **OR** - any pattern can match
- Between conditions (PathMatch AND PathExclude): **AND** - must satisfy both

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

### Monorepo with Selective Discovery

```yaml
# Discover only specific directories for faster performance
AutoDiscover:
  Enabled: true
  DiscoverDirs:
    - rtl
    - common
    - design/subsystem-a
    - design/subsystem-b

# Filter discovered files (optional)
If:
  PathExclude: .*/generated/.*

IncludeDirs:
  - common/include

Defines:
  - SYNTHESIS
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
