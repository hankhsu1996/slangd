# Semantic Index Migration TODO

## Phase 1: API Compatibility Layer ⏳

_Make SemanticIndex drop-in compatible with current separate indexes_

- [ ] **Add SymbolIndex-compatible API to SemanticIndex**

  - [ ] Add `GetDocumentSymbols(const std::string& uri) -> std::vector<lsp::DocumentSymbol>`
  - [ ] Implement document symbol hierarchy building from flat symbol map
  - [ ] Add URI filtering logic to match current SymbolIndex behavior

- [ ] **Add DefinitionIndex-compatible API to SemanticIndex**

  - [ ] Add `LookupSymbolAt(slang::SourceLocation) -> std::optional<SymbolKey>`
  - [ ] Add `GetDefinitionRange(const SymbolKey&) -> std::optional<slang::SourceRange>`
  - [ ] Add `GetDefinitionRanges() -> const std::unordered_map<SymbolKey, slang::SourceRange>&`
  - [ ] Add `GetReferenceMap() -> const std::unordered_map<slang::SourceRange, SymbolKey>&`

- [ ] **Build and test API compatibility**
  - [ ] Create unit tests for new API methods
  - [ ] Verify API matches current index behavior exactly

## Phase 2: Service Integration ⏳

_Update OverlaySession to use SemanticIndex_

- [ ] **Update OverlaySession constructor**

  - [ ] Replace 3 separate index creations with single `SemanticIndex::FromCompilation()`
  - [ ] Update member variables: remove `definition_index_`, `symbol_index_`, add `semantic_index_`
  - [ ] Keep `diagnostic_index_` separate (different data path)

- [ ] **Update OverlaySession getters**

  - [ ] Replace `GetDefinitionIndex()` with `GetSemanticIndex()`
  - [ ] Replace `GetSymbolIndex()` with `GetSemanticIndex()`
  - [ ] Add compatibility methods that delegate to SemanticIndex

- [ ] **Build and verify OverlaySession**
  - [ ] Ensure OverlaySession builds successfully
  - [ ] Verify all getter methods compile

## Phase 3: LanguageService Migration ⏳

_Update LanguageService to use unified API_

- [ ] **Update GetDocumentSymbols() method**

  - [ ] Replace `session->GetSymbolIndex().GetDocumentSymbols(uri)`
  - [ ] Use `session->GetSemanticIndex().GetDocumentSymbols(uri)`

- [ ] **Update GetDefinitionsForPosition() method**

  - [ ] Replace `session->GetDefinitionIndex().LookupSymbolAt()` chain
  - [ ] Use `session->GetSemanticIndex().LookupSymbolAt()` chain
  - [ ] Maintain exact same return behavior

- [ ] **Build and verify LanguageService**
  - [ ] Ensure LanguageService builds successfully
  - [ ] Verify LSP responses match previous behavior

## Phase 4: Integration Testing ⏳

_Validate migration preserves all functionality_

- [ ] **Update existing tests**

  - [ ] Modify `overlay_session_test.cpp` to use SemanticIndex
  - [ ] Update any tests that directly reference old indexes
  - [ ] Ensure all semantic tests pass

- [ ] **End-to-end validation**
  - [ ] Test document symbols LSP requests
  - [ ] Test go-to-definition LSP requests
  - [ ] Verify performance is equal or better

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

## Current Status

**✅ COMPLETED:**

- [x] Core SemanticIndex implementation with universal symbol coverage
- [x] Comprehensive test suite (3 test cases, 19 assertions passing)
- [x] Universal preVisit hook integration with slang ASTVisitor
- [x] O(1) symbol lookup with flat hash map storage
- [x] Correct LSP symbol kind mappings (EnumValue → kEnumMember)

**🎯 NEXT UP:** Phase 1 - API Compatibility Layer

**📍 INTEGRATION POINTS:**

- `OverlaySession::Create()` (lines 29-39 in overlay_session.cpp)
- `LanguageService::GetDocumentSymbols()` (line 185 in language_service.cpp)
- `LanguageService::GetDefinitionsForPosition()` (lines 127-142 in language_service.cpp)

**🔧 KEY FILES:**

- **Core:** `include/slangd/semantic/semantic_index.hpp`
- **Integration:** `src/slangd/services/overlay_session.cpp`
- **Usage:** `src/slangd/services/language_service.cpp`
