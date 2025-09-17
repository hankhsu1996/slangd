# Semantic Index Migration TODO

## Phase 1: API Compatibility Layer ⏳

_Make SemanticIndex drop-in compatible with current separate indexes_

- [x] **Add SymbolIndex-compatible API to SemanticIndex**

  - [x] Add `GetDocumentSymbols(const std::string& uri) -> std::vector<lsp::DocumentSymbol>`
  - [x] Implement document symbol hierarchy building from flat symbol map
  - [x] Add URI filtering logic to match current SymbolIndex behavior

- [x] **Add core definition data structures**

  - [x] Copy `SymbolKey` struct to semantic_index.hpp
  - [x] Add `definition_ranges_` map (SymbolKey -> SourceRange)
  - [x] Add `reference_map_` map (SourceRange -> SymbolKey)
  - [x] Add `is_definition` and `definition_range` to SymbolInfo
  - [x] Build and test - existing tests should pass

- [x] **Collect definition ranges in preVisit**

  - [x] Extract precise name ranges from syntax nodes
  - [x] Store in definition*ranges* map for ALL symbols
  - [x] Set is_definition flag in SymbolInfo
  - [x] Test definition ranges are collected correctly

- [x] **Add reference tracking**

  - [x] Add `handle(NamedValueExpression)` to IndexVisitor
  - [x] Store symbol references in reference*map*
  - [x] Test references are tracked correctly

- [x] **Add DefinitionIndex-compatible API**

  - [x] Add `LookupSymbolAt(slang::SourceLocation) -> std::optional<SymbolKey>`
  - [x] Add `GetDefinitionRange(const SymbolKey&) -> std::optional<slang::SourceRange>`
  - [x] Add `GetDefinitionRanges() -> const std::unordered_map<SymbolKey, slang::SourceRange>&`
  - [x] Add `GetReferenceMap() -> const std::unordered_map<slang::SourceRange, SymbolKey>&`
  - [x] Basic tests to verify API functionality

- [x] **Step 1: Enhanced Basic Testing Foundation**

  - [x] Create SemanticIndexFixture similar to DefinitionIndexFixture
  - [x] Add helper methods for precise SymbolKey creation
  - [x] Add SourceRange verification utilities
  - [x] Add better compilation and source management helpers
  - [x] Test one basic scenario first, then incrementally add assertions

- [x] **Step 2: Crash Resilience Testing**

  - [x] Port interface crash scenarios from definition_index_test.cpp
  - [x] Test interface ports with member access (bus.addr patterns)
  - [x] Test undefined interfaces for LSP single-file resilience
  - [x] Test interface expressions in always_comb conditions
  - [x] Test complex module/interface scenarios with createInvalid() patterns

- [x] **Step 3: API Compatibility Testing** _(Skipped - merged into Step 4)_

  - Note: Direct comparison with legacy indexes creates unwanted coupling
  - Important patterns from legacy tests ported to Step 4 instead
  - Avoids dependency on soon-to-be-removed DefinitionIndex/SymbolIndex

- [x] **Step 4: Complex SystemVerilog Patterns (Enhanced)**

  - [x] Test nested scopes and multiple declarations (ported from legacy)
  - [x] Test reference tracking in expressions (enhanced from legacy)
  - [x] Test typedef and enum definitions with hierarchical children
  - [x] Test package definitions (found issue: modules not indexed with packages)
  - [x] Test struct and union types
  - [x] Test module body traversal via preVisit hook
  - [x] Verify comprehensive coverage (103 assertions passing, found 1 bug)

- [x] **Step 5: Test Infrastructure Refactoring**

  - [x] Create test_fixtures.hpp with shared SemanticTestFixture infrastructure
  - [x] Add MultiFileSemanticFixture for multifile test support
  - [x] Update semantic_index_test.cpp to use new shared fixtures
  - [x] Update BUILD.bazel to include test_fixtures.hpp
  - [x] Verify all existing tests still pass

- [x] **Step 6: Test File Organization**

  - [x] Rename semantic_index_test.cpp to semantic_index_basic_test.cpp
  - [x] Extract complex patterns to semantic_index_patterns_test.cpp
  - [x] Keep basic functionality tests (5 test cases, ~400 lines)
  - [x] Move complex SystemVerilog patterns (6 test cases, ~500 lines)
  - [x] Verify all 11 test cases still pass after split

- [x] **Step 7: Multifile Testing Implementation**

  - [x] Create semantic_index_multifile_test.cpp with async infrastructure
  - [x] Test cross-package symbol resolution
  - [x] Test GlobalCatalog integration with SemanticIndex
  - [x] Test qualified package references (pkg::symbol patterns)
  - [x] Test interface cross-file references with crash resilience
  - [x] Test multi-package dependencies (transitive imports)
  - [x] Verify multifile symbol indexing across compilation units

## Phase 2: Service Integration ✅

_Update OverlaySession to use SemanticIndex_

- [x] **Update OverlaySession constructor**

  - [x] Replace 3 separate index creations with single `SemanticIndex::FromCompilation()`
  - [x] Update member variables: remove `definition_index_`, `symbol_index_`, add `semantic_index_`
  - [x] Keep `diagnostic_index_` separate (different data path)

- [x] **Update OverlaySession getters**

  - [x] Replace `GetDefinitionIndex()` with `GetSemanticIndex()`
  - [x] Replace `GetSymbolIndex()` with `GetSemanticIndex()`
  - [x] Add compatibility methods that delegate to SemanticIndex

- [x] **Build and verify OverlaySession**
  - [x] Ensure OverlaySession builds successfully
  - [x] Verify all getter methods compile

## Phase 3: LanguageService Migration ✅

_Update LanguageService to use unified API_

- [x] **Update GetDocumentSymbols() method**

  - [x] Replace `session->GetSymbolIndex().GetDocumentSymbols(uri)`
  - [x] Use `session->GetSemanticIndex().GetDocumentSymbols(uri)`

- [x] **Update GetDefinitionsForPosition() method**

  - [x] Replace `session->GetDefinitionIndex().LookupSymbolAt()` chain
  - [x] Use `session->GetSemanticIndex().LookupSymbolAt()` chain
  - [x] Maintain exact same return behavior

- [x] **Build and verify LanguageService**
  - [x] Ensure LanguageService builds successfully
  - [x] Verify LSP responses match previous behavior

## Phase 4: Integration Testing ✅

_Validate migration preserves all functionality_

- [x] **Update existing tests**

  - [x] Modify `definition_multifile_test.cpp` to use SemanticIndex
  - [x] Fix cross-package type reference resolution via VariableSymbol handler
  - [x] Update `overlay_session_test.cpp` to use SemanticIndex (if needed)
  - [x] Ensure all semantic tests pass (10/10 tests passing)

## Phase 5: Cleanup ⏳

_Remove obsolete code_

- [ ] **Remove old index files**

  - [ ] Delete `include/slangd/semantic/symbol_index.hpp`
  - [ ] Delete `src/slangd/semantic/symbol_index.cpp`
  - [ ] Delete `include/slangd/semantic/definition_index.hpp`
  - [ ] Delete `src/slangd/semantic/definition_index.cpp`

- [ ] **Remove old tests**

  - [ ] Delete `test/slangd/semantic/symbol_index_test.cpp`
  - [ ] Delete `test/slangd/semantic/definition_index_test.cpp`
  - [ ] Keep `definition_multifile_test.cpp` but update to use SemanticIndex

- [ ] **Update build files**
  - [ ] Remove old index targets from `BUILD.bazel`
  - [ ] Verify clean build with no references to old indexes
