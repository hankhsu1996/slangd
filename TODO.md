# Test Refactoring Migration TODO

This document provides step-by-step migration plan to improve test structure for better TDD workflow.

## Migration Overview

**Current state:** 8 test files, 2435 total lines, overengineered fixtures
**Target state:** 1:1 source mapping, simple fixtures, controlled logging

## Phase 1: Add Environment-Based Logging Control

**Goal:** Enable `SPDLOG_LEVEL=debug` for debugging without hardcoded levels

- [x] **Step 1.1:** Update all main() functions to read SPDLOG_LEVEL
  - [x] Update `test/slangd/core/project_layout_service_test.cpp:12`
  - [x] Update `test/slangd/semantic/definition_extractor_test.cpp:9`
  - [x] Update `test/slangd/semantic/definition_multifile_test.cpp:17`
  - [x] Update `test/slangd/semantic/semantic_index_basic_test.cpp:16`
  - [x] Update `test/slangd/semantic/semantic_index_multifile_test.cpp:18`
  - [x] Update `test/slangd/semantic/semantic_index_patterns_test.cpp:14`
  - [x] Update `test/slangd/services/global_catalog_test.cpp:16`
  - [x] Update `test/slangd/services/overlay_session_test.cpp:14`

**Change pattern:**

```cpp
// From:
spdlog::set_level(spdlog::level::debug);

// To:
if (auto* level = std::getenv("SPDLOG_LEVEL")) {
  spdlog::set_level(spdlog::level::from_str(level));
} else {
  spdlog::set_level(spdlog::level::warn);
}
```

- [x] **Step 1.2:** Verify all tests pass: `bazel test //test/...`
- [x] **Step 1.3:** Test logging control: `SPDLOG_LEVEL=debug bazel test //test/slangd:semantic_index_basic_test`

## Phase 2: Create Simple Test Fixture

**Goal:** Provide lightweight alternative to overengineered SemanticTestFixture

- [x] **Step 2.1:** Create `test/slangd/common/simple_fixture.hpp`

```cpp
#pragma once
#include <memory>
#include <string>
#include <optional>
#include <slang/text/SourceLocation.h>
#include "slangd/semantic/semantic_index.hpp"

namespace slangd::test {

class SimpleTestFixture {
public:
  // Compile source and return semantic index
  auto CompileSource(const std::string& code) -> std::unique_ptr<semantic::SemanticIndex>;

  // Find symbol location in source by name (must be unique)
  auto FindSymbol(const std::string& code, const std::string& name) -> slang::SourceLocation;

  // Get definition range for symbol at location
  auto GetDefinitionRange(semantic::SemanticIndex* index, slang::SourceLocation loc)
      -> std::optional<slang::SourceRange>;

private:
  std::shared_ptr<slang::SourceManager> source_manager_;
  std::unique_ptr<slang::ast::Compilation> compilation_;
  slang::BufferID buffer_id_;
};

} // namespace slangd::test
```

- [x] **Step 2.2:** Implement `test/slangd/common/simple_fixture.cpp`
- [x] **Step 2.3:** Add to `test/slangd/BUILD.bazel`:

```python
cc_library(
    name = "simple_fixture",
    srcs = ["common/simple_fixture.cpp"],
    hdrs = ["common/simple_fixture.hpp"],
    deps = [
        "//:slangd_core",
        "@slang",
    ],
)
```

- [x] **Step 2.4:** Convert `definition_extractor_test.cpp` to use SimpleTestFixture
- [x] **Step 2.5:** Verify test passes: `bazel test //test/slangd:definition_extractor_test`

## Phase 3: Split semantic_index_basic_test.cpp (875 lines → 4 focused files)

**Current file has 19 test cases covering too many concerns**

- [ ] **Step 3.1:** Create `test/slangd/semantic/semantic_index_test.cpp` (core functionality)

  Move these test cases from `semantic_index_basic_test.cpp`:

  - [ ] "SemanticIndex processes symbols via preVisit hook"
  - [ ] "SemanticIndex provides O(1) symbol lookup"
  - [ ] "SemanticIndex tracks references correctly"
  - [ ] "SemanticIndex basic definition tracking with fixture"
  - [ ] "SemanticIndex LookupDefinitionAt method exists and returns optional"

  Target size: ~150 lines

- [ ] **Step 3.2:** Create `test/slangd/semantic/document_symbol_builder_test.cpp` (document symbols)

  Move these test cases:

  - [ ] "SemanticIndex GetDocumentSymbols with enum hierarchy"
  - [ ] "SemanticIndex GetDocumentSymbols includes struct fields"
  - [ ] "SemanticIndex handles symbols with empty names for VSCode compatibility"
  - [ ] "SemanticIndex filters out genvar loop variables from document symbols"
  - [ ] "SemanticIndex function internals not in document symbols but available for definition lookup"

  Target size: ~200 lines

- [ ] **Step 3.3:** Create `test/slangd/semantic/generate_block_test.cpp` (SystemVerilog generate constructs)

  Move these test cases:

  - [ ] "SemanticIndex collects symbols inside generate if blocks"
  - [ ] "SemanticIndex collects symbols inside generate for loops"
  - [ ] "SemanticIndex filters out truly empty generate blocks"
  - [ ] "SemanticIndex preserves generate blocks with assertions"
  - [ ] "SemanticIndex properly handles assertion symbols in generate blocks"

  Target size: ~200 lines

- [ ] **Step 3.4:** Create `test/slangd/semantic/type_handling_test.cpp` (enum/struct/typedef handling)

  Move these test cases:

  - [ ] "SemanticIndex handles enum and struct types"
  - [ ] "SemanticIndex collects definition ranges correctly"
  - [ ] "SemanticIndex DefinitionIndex-compatible API basic functionality"
  - [ ] "SemanticIndex collects functions and tasks correctly"

  Target size: ~150 lines

- [ ] **Step 3.5:** Update `test/slangd/BUILD.bazel` with new test targets
- [ ] **Step 3.6:** Delete `test/slangd/semantic/semantic_index_basic_test.cpp`
- [ ] **Step 3.7:** Verify all tests pass: `bazel test //test/slangd/semantic:all`

## Phase 4: Create Missing Unit Tests

**Fill gaps in test coverage for untested source files**

- [ ] **Step 4.1:** Create `test/slangd/semantic/diagnostic_index_test.cpp`

  Tests for `src/slangd/semantic/diagnostic_index.cpp`:

  - [ ] Basic diagnostic collection and indexing
  - [ ] Diagnostic range extraction
  - [ ] Error/warning filtering

  Target size: ~100 lines

- [ ] **Step 4.2:** Create `test/slangd/semantic/symbol_utils_test.cpp`

  Tests for `src/slangd/semantic/symbol_utils.cpp`:

  - [ ] Symbol kind conversion utilities
  - [ ] Symbol name extraction
  - [ ] Location utilities

  Target size: ~100 lines

- [ ] **Step 4.3:** Update BUILD.bazel with new test targets
- [ ] **Step 4.4:** Verify tests pass: `bazel test //test/slangd/semantic:diagnostic_index_test //test/slangd/semantic:symbol_utils_test`

## Phase 5: Organize Integration Tests

**Separate unit tests from integration tests**

- [ ] **Step 5.1:** Create `test/slangd/semantic/integration/` directory
- [ ] **Step 5.2:** Move `definition_multifile_test.cpp` → `integration/multifile_definition_test.cpp`
- [ ] **Step 5.3:** Move `semantic_index_multifile_test.cpp` → `integration/multifile_reference_test.cpp`
- [ ] **Step 5.4:** Update `test/slangd/BUILD.bazel` paths
- [ ] **Step 5.5:** Verify integration tests pass: `bazel test //test/slangd/semantic/integration:all`

## Phase 6: Simplify Test Fixtures

**Remove overengineering from test utilities**

- [ ] **Step 6.1:** Audit `test_fixtures.hpp` usage

  - [ ] Identify which tests actually need MultiFileSemanticFixture
  - [ ] Convert simple cases to use SimpleTestFixture

- [ ] **Step 6.2:** Remove duplicate `MultiFileTestFixture` from `definition_multifile_test.cpp`

  - [ ] Use existing `MultiFileSemanticFixture` instead

- [ ] **Step 6.3:** Simplify `test_fixtures.hpp`

  - [ ] Remove unused methods and complexity
  - [ ] Keep only multifile builder pattern for integration tests

- [ ] **Step 6.4:** Verify all tests still pass: `bazel test //test/...`

## Phase 7: Validation and Documentation

**Ensure migration success and document new structure**

- [ ] **Step 7.1:** Verify test structure matches source structure:

```
src/slangd/semantic/           test/slangd/semantic/
├── definition_extractor.cpp  ├── definition_extractor_test.cpp     ✓
├── diagnostic_index.cpp       ├── diagnostic_index_test.cpp        ✓
├── document_symbol_builder.cpp├── document_symbol_builder_test.cpp ✓
├── semantic_index.cpp         ├── semantic_index_test.cpp          ✓
└── symbol_utils.cpp           └── symbol_utils_test.cpp            ✓
```

- [ ] **Step 7.2:** Verify test size targets:

  - [ ] No test file > 300 lines
  - [ ] No test case > 50 lines
  - [ ] All tests focused on single aspect

- [ ] **Step 7.3:** Test logging control:

```bash
# Default (quiet)
bazel test //test/slangd/semantic:semantic_index_test

# Debug when needed
SPDLOG_LEVEL=debug bazel test //test/slangd/semantic:semantic_index_test --test_output=all

# Info for development
SPDLOG_LEVEL=info bazel test //test/slangd/semantic:semantic_index_test --test_output=all
```

- [ ] **Step 7.4:** Create `test/README.md` with:

  - [ ] Test organization guide
  - [ ] How to use SimpleTestFixture
  - [ ] When to use MultiFileSemanticFixture
  - [ ] SPDLOG_LEVEL usage examples
  - [ ] TDD workflow examples

- [ ] **Step 7.5:** Run full test suite: `bazel test //test/...`
- [ ] **Step 7.6:** Update CLAUDE.md with new testing guidelines

## Success Criteria

After completing all phases:

✅ **Intuitive TDD workflow:** Working on `definition_extractor.cpp` → go to `definition_extractor_test.cpp`
✅ **Manageable test files:** All files <300 lines, focused on single concern
✅ **Simple unit testing:** `SimpleTestFixture` for basic "compile and check" tests
✅ **Controlled logging:** `SPDLOG_LEVEL` environment variable for debugging
✅ **Clear separation:** Unit tests vs integration tests in separate directories
✅ **Future-ready:** Structure supports rename, semantic tokens, hover features

## Rollback Plan

If any phase fails:

1. `git checkout` to previous working state
2. Run `bazel test //test/...` to verify
3. Debug specific failing tests
4. Fix issues before proceeding to next phase

Each phase is designed to be independently reversible.
