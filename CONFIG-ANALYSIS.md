# ConfigManager Architecture Analysis

## Overview

This analysis documents the current ConfigManager architecture in the slangd project to understand the dependencies and data flow for a planned refactoring from config-driven to auto-discovery mode.

## 1. ConfigManager Integration Points

### 1.1 Core Architecture

The ConfigManager is at the heart of the slangd initialization flow:

```
main.cpp → SlangdLspServer → ConfigManager → {DocumentManager, WorkspaceManager} → FeatureProviders
```

### 1.2 File Locations

- **Header**: `/home/hankhsu/workspace/c++/slangd/include/slangd/core/config_manager.hpp`
- **Implementation**: `/home/hankhsu/workspace/c++/slangd/src/slangd/core/config_manager.cpp`
- **Config File**: `/home/hankhsu/workspace/c++/slangd/include/slangd/core/slangd_config_file.hpp`

### 1.3 Direct Dependencies

ConfigManager is **directly required** by:

1. **SlangdLspServer** (`slangd_lsp_server.cpp:40-49`)
   - Created during `OnInitialize()` 
   - Passed to both DocumentManager and WorkspaceManager
   - Used for config file change handling

2. **DocumentManager** (`document_manager.hpp:22`, `document_manager.cpp:24`)
   - Takes `std::shared_ptr<ConfigManager>` in constructor
   - Uses config for preprocessing options during parsing

3. **WorkspaceManager** (`workspace_manager.hpp:26`, `workspace_manager.cpp:20`)
   - Takes `std::shared_ptr<ConfigManager>` in constructor
   - Uses config for file discovery and workspace scanning

## 2. Manager Dependencies Details

### 2.1 DocumentManager Dependencies

**File**: `src/slangd/core/document_manager.cpp`

**Usage Points**:
- **Lines 40-45**: Gets preprocessor defines and include directories for Slang parsing
  ```cpp
  for (const auto& define : config_manager_->GetDefines()) {
    pp_options.predefines.push_back(define);
  }
  for (const auto& include_dir : config_manager_->GetIncludeDirectories()) {
    pp_options.additionalIncludePaths.emplace_back(include_dir.Path());
  }
  ```

**Critical Functionality**:
- **Preprocessor Defines**: Used to set macros for SystemVerilog compilation
- **Include Directories**: Used to resolve `include statements in SystemVerilog files
- **Parsing Options**: Both are fed into Slang's `PreprocessorOptions`

### 2.2 WorkspaceManager Dependencies

**File**: `src/slangd/core/workspace_manager.cpp`

**Usage Points**:
- **Line 138**: `auto all_files = config_manager_->GetSourceFiles()` - Core file discovery
- **Lines 277-285**: File filtering during workspace changes based on config
- **TestingFactory**: Creates default ConfigManager for test environments (line 39)

**Critical Functionality**:
- **File Discovery**: Primary source of which files to include in workspace
- **File Filtering**: Determines which file changes are relevant
- **Workspace Scanning**: Drives the initial workspace indexing process

### 2.3 Feature Providers (Indirect Dependencies)

Feature providers don't directly depend on ConfigManager but rely on it indirectly through DocumentManager and WorkspaceManager:

- **DiagnosticsProvider**: Uses DocumentManager's parsed compilation (with config-based preprocessor options)
- **DefinitionProvider**: Uses both managers which depend on ConfigManager
- **SymbolsProvider**: Uses both managers which depend on ConfigManager

## 3. Current Initialization Flow

### 3.1 LSP Server Startup Flow

1. **main.cpp**: Creates SlangdLspServer with executor and logger
2. **OnInitialize()** (`slangd_lsp_server.cpp:27-82`):
   ```cpp
   // Create ConfigManager first
   config_manager_ = std::make_shared<ConfigManager>(executor_, workspace_uri);
   
   // Load configuration (may return false if no config found)
   co_await config_manager_->LoadConfig(workspace_uri);
   
   // Create managers with ConfigManager dependency
   document_manager_ = std::make_shared<DocumentManager>(executor_, config_manager_);
   workspace_manager_ = std::make_shared<WorkspaceManager>(executor_, workspace_uri, config_manager_);
   
   // Create feature providers (indirect dependency)
   definition_provider_ = std::make_unique<DefinitionProvider>(document_manager_, workspace_manager_);
   // ... other providers
   ```

3. **OnInitialized()** (`slangd_lsp_server.cpp:84-129`):
   - Triggers `workspace_manager_->ScanWorkspace()` 
   - Registers file system watchers
   - Background workspace indexing begins

### 3.2 Config Loading Behavior

**Success Path** (`config_manager.cpp:22-53`):
- Looks for `.slangd` file in workspace root
- Parses YAML configuration using yaml-cpp
- Logs configuration details (includes, defines, files, file lists)
- Returns `true`

**No Config Path**:
- **LoadConfig()** returns `false` but doesn't fail
- **ConfigManager** continues with `config_.reset()` (empty optional)
- Fallback behavior kicks in for file discovery and settings

### 3.3 Config Change Handling

**File Watcher** (`slangd_lsp_server.cpp:326-360`):
- Watches `**/.slangd` files via LSP file system watcher
- Calls `config_manager_->HandleConfigFileChange()` on changes
- If config updated, triggers `workspace_manager_->ScanWorkspace()` 
- Rescans entire workspace with new settings

## 4. Configuration Data Usage Analysis

### 4.1 SlangdConfigFile Structure

**File**: `slangd_config_file.hpp`

```yaml
# Example .slangd configuration
FileLists:
  Paths: ["project.f", "testbench.f"] 
  Absolute: false

Files: 
  - "src/main.sv"
  - "src/utils.sv"

IncludeDirs:
  - "include/"
  - "vendor/include/"

Defines:
  - "SIMULATION"
  - "DEBUG=1"
```

### 4.2 Data Usage by Component

| **Config Data** | **Used By** | **Purpose** | **Criticality** | **Auto-Discovery Potential** |
|---|---|---|---|---|
| **Files** | WorkspaceManager | Direct file list for compilation | High | **Yes** - recursive file scanning |
| **FileLists** (.f files) | WorkspaceManager | File list references | High | **Partial** - could scan for .f files |
| **IncludeDirs** | DocumentManager | Preprocessor include paths | High | **Yes** - scan for common include patterns |
| **Defines** | DocumentManager | Preprocessor macros | Medium | **No** - requires explicit specification |

### 4.3 Fallback Behavior (Auto-Discovery Mode)

**When no config exists** (`config_manager.cpp`):

1. **GetSourceFiles()** (Lines 93-131):
   - Falls back to `FindSystemVerilogFilesInDirectory(workspace_root_)`
   - Recursively scans workspace for `.sv`, `.svh`, `.v`, `.vh` files
   - Uses `IsSystemVerilogFile()` from `path_utils.cpp:24-26`

2. **GetIncludeDirectories()** (Lines 133-154):
   - Falls back to **ALL directories in workspace** via `recursive_directory_iterator`
   - Very broad but safe approach

3. **GetDefines()** (Lines 156-165):
   - Falls back to **empty list** 
   - No auto-discovery for preprocessor defines

## 5. Refactoring Impact Assessment

### 5.1 Big-Bang Refactor Requirements

To remove ConfigManager dependencies entirely:

1. **DocumentManager Changes**:
   - Remove `std::shared_ptr<ConfigManager>` parameter from constructor
   - Replace `config_manager_->GetDefines()` with auto-discovered or default defines
   - Replace `config_manager_->GetIncludeDirectories()` with auto-discovered includes
   - **Impact**: Moderate - need alternative source for preprocessing options

2. **WorkspaceManager Changes**:
   - Remove `std::shared_ptr<ConfigManager>` parameter from constructor  
   - Replace `config_manager_->GetSourceFiles()` with direct auto-discovery
   - Remove config-based file filtering in `HandleFileCreated()`
   - **Impact**: High - core file discovery logic change

3. **SlangdLspServer Changes**:
   - Remove ConfigManager creation and lifecycle management
   - Remove config file change handling
   - Update manager constructors to not require ConfigManager
   - **Impact**: Moderate - simplification of initialization

4. **Test Infrastructure**:
   - Update all test cases that create ConfigManager instances
   - Replace config-based test setups with direct file specification
   - **Impact**: Low - tests should become simpler

### 5.2 Alternative Auto-Discovery Approaches

1. **File Discovery**:
   - **Current**: Config specifies files OR fallback to recursive scan
   - **Proposed**: Always use recursive scan with smart filtering
   - **Enhancement**: Respect `.gitignore`, common build directories

2. **Include Directory Discovery**:
   - **Current**: Config specifies OR all directories fallback
   - **Proposed**: Heuristic-based discovery (common patterns like `include/`, `inc/`, `src/`)
   - **Enhancement**: Parse existing files for `include paths

3. **Preprocessor Defines**:
   - **Current**: Config specifies OR empty fallback  
   - **Proposed**: Empty by default (safest approach)
   - **Future**: Parse common build files (Makefiles, etc.) for defines

### 5.3 Migration Strategy

**Phase 1 - Preparation**:
- Add auto-discovery logic to ConfigManager as fallback enhancement
- Test auto-discovery thoroughly with existing config-based systems
- Ensure feature parity with current fallback behavior

**Phase 2 - Big-Bang Refactor**:
- Remove ConfigManager dependency from constructors
- Move auto-discovery logic directly into managers
- Update all call sites and tests simultaneously  
- Remove ConfigManager and SlangdConfigFile classes

**Phase 3 - Enhancement**:
- Improve auto-discovery heuristics
- Add user-configurable auto-discovery settings via LSP workspace configuration
- Implement smarter include directory detection

### 5.4 Risk Assessment

**High Risk**:
- **Preprocessing behavior changes**: Different include paths or defines could break user projects
- **File discovery differences**: Auto-discovery might include/exclude different files than config

**Medium Risk**:
- **Performance impact**: Recursive directory scanning vs. explicit file lists
- **Test compatibility**: Extensive test suite updates required

**Low Risk**:  
- **LSP protocol compatibility**: No changes to LSP interface
- **Feature functionality**: Core language features should remain unchanged

## 6. Recommendations

### 6.1 Implementation Order

1. **Start with WorkspaceManager**: File discovery is the most isolated change
2. **Then DocumentManager**: Preprocessing options have clear fallback strategies  
3. **Finally SlangdLspServer**: Remove config management infrastructure
4. **Update tests last**: Once core changes are stabilized

### 6.2 Preservation Strategy

Consider keeping ConfigManager as an **optional** enhancement rather than removing entirely:
- Auto-discovery by default
- Optional config file support for advanced users
- Gradual migration path instead of big-bang removal

This would reduce risk while providing the auto-discovery benefits for most users.

---

**Analysis completed**: Ready for refactoring planning phase.