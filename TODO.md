# SemanticIndex Refactoring TODO

## Phase 1: Extract Definition Range Logic 🎯 ✅

### Goal: Move definition extraction to separate component while maintaining single traversal

- [x] Create `include/slangd/semantic/definition_extractor.hpp`

  - [x] Define `DefinitionExtractor` class with static methods
  - [x] Move `ExtractDefinitionRange` method signature
  - [x] Add private methods for each symbol type (Package, Module, TypeAlias, Variable, etc.)

- [x] Create `src/slangd/semantic/definition_extractor.cpp`

  - [x] Implement `ExtractDefinitionRange` with symbol type switch
  - [x] Move existing symbol-specific extraction logic from `semantic_index.cpp`
  - [x] Extract methods: `ExtractPackageRange`, `ExtractModuleRange`, `ExtractTypedefRange`, etc.

- [x] Update `semantic_index.cpp`

  - [x] Add `#include "slangd/semantic/definition_extractor.hpp"`
  - [x] Replace direct extraction with `DefinitionExtractor::ExtractDefinitionRange()`
  - [x] Remove moved extraction logic

- [x] Update `BUILD.bazel`

  - [x] Add `definition_extractor.cpp` to semantic library
  - [x] Add header dependency

- [x] **Verification**: Build and run all tests - ensure 100% pass rate

---

## Phase 2: Extract Document Symbol Building 📊 ✅

### Goal: Move hierarchical tree construction to separate component

- [x] Create `include/slangd/semantic/document_symbol_builder.hpp`

  - [x] Define `DocumentSymbolBuilder` class with static methods
  - [x] Move method signatures: `BuildDocumentSymbolTree`, `CreateDocumentSymbol`, etc.

- [x] Create `src/slangd/semantic/document_symbol_builder.cpp`

  - [x] Move `BuildDocumentSymbolTree` implementation
  - [x] Move helper methods: `CreateDocumentSymbol`, `AttachChildrenToSymbol`
  - [x] Move special handlers: `HandleEnumTypeAlias`, `HandleStructTypeAlias`
  - [x] Add parameter for symbols map and source manager

- [x] Update `semantic_index.cpp`

  - [x] Add `#include "slangd/semantic/document_symbol_builder.hpp"`
  - [x] Replace `GetDocumentSymbols` implementation with delegation
  - [x] Remove moved tree construction logic

- [x] Update `BUILD.bazel`

  - [x] Add `document_symbol_builder.cpp` to semantic library

- [x] **Verification**: Build and run all tests - ensure 100% pass rate

---

## Phase 3: Eliminate Code Duplication and Clean Architecture 🧹 ✅

### Goal: Create shared utilities and eliminate duplicate symbol processing logic

- [x] Create shared symbol utilities module

  - [x] Create `include/slangd/semantic/symbol_utils.hpp`
  - [x] Create `src/slangd/semantic/symbol_utils.cpp`
  - [x] Move `ComputeLspRange` and `ShouldIndex` to shared utilities
  - [x] Create unified `ConvertToLspKind` with comprehensive and simplified variants

- [x] **Verification Step 1**: Build and run all tests - ensure no regressions

- [x] Update SemanticIndex to use shared utilities

  - [x] Replace duplicate `ComputeLspRange` with shared version
  - [x] Replace duplicate `ShouldIndex` with shared version
  - [x] Update `ConvertToLspKind` to use comprehensive shared version
  - [x] Update includes and remove duplicate implementations

- [x] **Verification Step 2**: Build and run all tests - ensure functionality preserved

- [x] Update DocumentSymbolBuilder to use shared utilities

  - [x] Replace duplicate `ComputeLspRange` with shared version
  - [x] Replace duplicate `ShouldIndex` with document-specific logic where needed
  - [x] Update `ConvertToLspKind` to use simplified shared version
  - [x] Update includes and remove duplicate implementations

- [x] **Verification Step 3**: Build and run all tests - ensure document symbols work correctly

- [x] Update documentation and comments

  - [x] Fix "DefinitionIndex" reference in `language_service.hpp` to "SemanticIndex"
  - [x] Update class documentation to reflect shared utilities
  - [x] Clean up outdated comments in implementation files

- [x] Update BUILD.bazel files

  - [x] Add `symbol_utils.cpp` to semantic library (automatically included via glob)
  - [x] Update dependencies as needed

- [x] **Final Verification**: Build and run all tests - ensure 100% pass rate

---

## Phase 4: Refactor Test Infrastructure 🧪

### Goal: Focus tests on LSP behavior rather than implementation details

- [ ] Create `test/slangd/semantic/test_utilities.hpp`

  - [ ] Move shared test fixtures
  - [ ] Add LSP-focused helper methods: `ContainsSymbol`, `HasDefinition`, etc.
  - [ ] Remove internal implementation test helpers

- [ ] Update `semantic_index_basic_test.cpp`

  - [ ] Replace symbol count tests with LSP behavior tests
  - [ ] Use helper methods for cleaner assertions
  - [ ] Focus on `GetDocumentSymbols` and `GetDefinitionRange` API

- [ ] Update `semantic_index_patterns_test.cpp`

  - [ ] Simplify complex validation loops
  - [ ] Use boolean assertions for key symbol presence
  - [ ] Remove exhaustive symbol enumeration tests

- [ ] Update `semantic_index_multifile_test.cpp`

  - [ ] Keep the cleaner test patterns you identified
  - [ ] Remove complex internal validation
  - [ ] Focus on URI filtering and cross-file behavior

- [ ] **Verification**: Build and run all tests - ensure 100% pass rate with cleaner test code

---

## Phase 5: Final Cleanup and Documentation 📝

### Goal: Polish the refactored architecture

- [ ] Update architecture documentation

  - [ ] Update comments in header files
  - [ ] Add component interaction diagrams
  - [ ] Document the single-traversal approach

- [ ] Performance verification

  - [ ] Run performance benchmarks if available
  - [ ] Verify O(1) lookup performance maintained
  - [ ] Confirm single traversal efficiency

- [ ] Code quality checks

  - [ ] Run clang-format on all modified files
  - [ ] Run clang-tidy and fix any issues
  - [ ] Review include dependencies

- [ ] **Final Verification**: Full build and test suite - ensure no regressions

---

## Success Criteria

After completion, the refactored SemanticIndex should achieve:

✅ **Maintained Functionality**:

- All existing tests pass without modification
- LSP features work identically to before
- Performance characteristics unchanged

✅ **Improved Architecture**:

- Core `semantic_index.cpp` reduced from 687 lines to ~300 lines
- Clear separation of concerns across components
- Maintainable and extensible codebase

✅ **Better Tests**:

- Tests focus on LSP API behavior
- Simpler assertions with clear pass/fail criteria
- Reduced test complexity and maintenance burden

## Notes

- **Incremental Approach**: Each phase maintains full compilation and test success
- **Backward Compatibility**: Public API remains unchanged throughout refactoring
- **Single Traversal Preserved**: The core efficiency of one compilation pass is maintained
- **Component Independence**: Each extracted component can be tested and modified independently
