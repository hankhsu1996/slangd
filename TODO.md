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

## Phase 2: Extract Document Symbol Building 📊

### Goal: Move hierarchical tree construction to separate component

- [ ] Create `include/slangd/semantic/document_symbol_builder.hpp`
  - [ ] Define `DocumentSymbolBuilder` class with static methods
  - [ ] Move method signatures: `BuildDocumentSymbolTree`, `CreateDocumentSymbol`, etc.

- [ ] Create `src/slangd/semantic/document_symbol_builder.cpp`
  - [ ] Move `BuildDocumentSymbolTree` implementation
  - [ ] Move helper methods: `CreateDocumentSymbol`, `AttachChildrenToSymbol`
  - [ ] Move special handlers: `HandleEnumTypeAlias`, `HandleStructTypeAlias`
  - [ ] Add parameter for symbols map and source manager

- [ ] Update `semantic_index.cpp`
  - [ ] Add `#include "slangd/semantic/document_symbol_builder.hpp"`
  - [ ] Replace `GetDocumentSymbols` implementation with delegation
  - [ ] Remove moved tree construction logic

- [ ] Update `BUILD.bazel`
  - [ ] Add `document_symbol_builder.cpp` to semantic library

- [ ] **Verification**: Build and run all tests - ensure 100% pass rate

---

## Phase 3: Simplify Core SemanticIndex 🧹

### Goal: Clean up core index to focus on storage and visitor orchestration

- [ ] Refactor `IndexVisitor` class in `semantic_index.cpp`
  - [ ] Add constructor parameters for extracted components
  - [ ] Simplify `ProcessSymbol` method to delegate to extractors
  - [ ] Clean up method signatures and documentation

- [ ] Update `FromCompilation` method
  - [ ] Create instances of extracted components
  - [ ] Pass components to visitor constructor
  - [ ] Maintain single traversal pattern

- [ ] Clean up `semantic_index.hpp`
  - [ ] Remove moved method declarations
  - [ ] Add forward declarations for extracted components
  - [ ] Update class documentation

- [ ] Clean up `semantic_index.cpp`
  - [ ] Remove dead code and comments
  - [ ] Simplify includes
  - [ ] Improve code organization

- [ ] **Verification**: Build and run all tests - ensure 100% pass rate

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