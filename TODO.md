# Test Modernization & Reorganization Plan

## Overview
Comprehensive plan to modernize and reorganize the semantic test suite for better maintainability, consistency, and LSP protocol alignment.

## Phase 1: Simple Consolidation & API Modernization

### 1.1 Test File Consolidation
- **Merge `definition_extractor_test.cpp`** into `semantic_index_test.cpp`
  - Only contains 1 test (51 lines) about parameter definition ranges
  - Better to consolidate related functionality

### 1.2 API Modernization
- **Convert all tests to high-level APIs**: Replace manual loops with fixture methods:
  - `AssertSymbolAtLocation(index, code, symbol_name, expected_kind)`
  - `AssertGoToDefinition(index, code, symbol_name, ref_index, def_index)`
  - `AssertReferenceExists(index, code, symbol_name, ref_index)`
- **Make low-level fixture APIs private**: Mark internal methods as private
- **Audit for missing high-level methods**: Add any patterns still requiring manual work

### 1.3 Current Manual Patterns to Modernize
Found in `semantic_index_patterns_test.cpp`:
```cpp
// Current manual pattern:
for (const auto& [loc, info] : index.GetAllSymbols()) {
  names.emplace_back(info.symbol->name);
}

// Should become high-level fixture method
```

## Phase 2: LSP Protocol Naming Alignment

Rename test files to match official LSP protocol methods:

| Current Name | New Name | LSP Method |
|--------------|----------|------------|
| `semantic_index_test.cpp` | `definition_test.cpp` | `textDocument/definition` |
| `document_symbol_builder_test.cpp` | `document_symbol_test.cpp` | `textDocument/documentSymbol` |

Keep:
- `semantic_index_patterns_test.cpp` - for complex edge cases
- Integration tests in `/integration/` - already well organized

## Phase 3: Fixture Consistency Decision

### Current Inconsistency Issue
- **SimpleTestFixture**: Header + Implementation (.hpp + .cpp)
- **SemanticTestFixture/MultiFileSemanticFixture**: Header-only (.hpp)

### Decision Needed
Should we make `SemanticTestFixture` also header+implementation for consistency?

**Pros of header+impl for SemanticTestFixture:**
- Consistent with SimpleTestFixture pattern
- Cleaner separation of interface and implementation
- Faster compilation (less code in headers)

**Cons:**
- More files to maintain
- SemanticTestFixture is more specialized/less used

## Phase 4: Testing & Validation

### 4.1 After Each Phase
1. **Run full test suite**: `bazel test //test/slangd/semantic/...`
2. **Update BUILD files** for any renamed/merged files
3. **Verify no functionality lost**

### 4.2 BUILD File Updates Needed
- Remove `definition_extractor_test` target
- Add content to `semantic_index_test` target
- Rename targets to match new file names

## Current Test Organization Analysis

### Feature-Centric vs Construct-Centric
**Recommendation**: Feature-centric organization (LSP capabilities)
- Scales better with growth (10 features vs 30+ constructs)
- Aligns with industry standards (rust-analyzer, clangd, etc.)
- Better for debugging and CI failure categorization

### Current Test File Responsibilities
- `semantic_index_test.cpp`: Comprehensive SV construct testing for go-to-definition
- `semantic_index_patterns_test.cpp`: Complex SystemVerilog patterns and edge cases
- `document_symbol_builder_test.cpp`: LSP document symbol hierarchy
- Integration tests: Multi-file scenarios

## Implementation Notes

### Fixture Usage Mapping
- **SimpleTestFixture**: Used by all single-file semantic tests (8 files)
- **SemanticTestFixture**: Used in `/integration/multifile_reference_test.cpp`
- **MultiFileSemanticFixture**: Used in `/integration/multifile_definition_test.cpp`
- **FileTestFixture**: Base for file management

All fixtures are actively used - no unused code found.

### Key Benefits
- **LSP protocol naming alignment**: Clearer test file purposes
- **Consistent high-level API usage**: Better maintainability
- **Reduced manual testing patterns**: More reliable tests
- **Preserved fixture functionality**: No breaking changes to integration tests

## Future Considerations

### Potential Future Structure (Post-Phase 2)
```
test/slangd/semantic/
├── definition_test.cpp          # textDocument/definition
├── document_symbol_test.cpp     # textDocument/documentSymbol  
├── references_test.cpp          # textDocument/references (future)
├── diagnostics_test.cpp         # textDocument/publishDiagnostics (future)
├── patterns_test.cpp            # Complex edge cases
└── integration/                 # Multi-file scenarios
    ├── multifile_definition_test.cpp
    └── multifile_reference_test.cpp
```

This aligns with the 10 features × 30 constructs scaling challenge by organizing around the 10 LSP features rather than 30+ constructs.