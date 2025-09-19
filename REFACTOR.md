# Test Structure Refactoring

This document outlines the philosophy and approach for refactoring the test structure to support better TDD workflows.

## Current Problems

### 1. Test File Naming Issues

Current test files don't map 1:1 to source files, making TDD unintuitive:

**Source files:**

```
src/slangd/semantic/
├── definition_extractor.cpp
├── diagnostic_index.cpp
├── document_symbol_builder.cpp
├── semantic_index.cpp
└── symbol_utils.cpp
```

**Current test files:**

```
test/slangd/semantic/
├── definition_extractor_test.cpp     ✓ Good mapping
├── definition_multifile_test.cpp     ✗ Not a source file
├── semantic_index_basic_test.cpp     ✗ Too broad, 875 lines!
├── semantic_index_multifile_test.cpp ✗ Integration test mixed with unit tests
├── semantic_index_patterns_test.cpp  ✗ Too specific
└── test_fixtures.hpp                 ✗ Overengineered, 442 lines
```

**Missing unit tests:**

- `diagnostic_index_test.cpp`
- `document_symbol_builder_test.cpp`
- `symbol_utils_test.cpp`

### 2. Overengineered Test Fixtures

**Current SemanticTestFixture (442 lines):**

- Complex builder pattern for simple cases
- FileRole enum, IndexBuilder class
- Most tests just need "compile this code and check result"

**Current test duplication:**

- 8 test files × ~15 lines of main() = 120 lines duplicated
- Multiple fixture classes doing similar things

### 3. Test File Size Issues

- `semantic_index_basic_test.cpp`: 875 lines, 19 test cases
- Tests are hard to find and understand
- Mixed concerns (document symbols + definition lookup + generate blocks)

## New Test Structure Philosophy

### 1. 1:1 Source-to-Test Mapping

**Target structure:**

```
test/slangd/semantic/
├── definition_extractor_test.cpp      # Tests definition_extractor.cpp
├── diagnostic_index_test.cpp          # Tests diagnostic_index.cpp
├── document_symbol_builder_test.cpp   # Tests document_symbol_builder.cpp
├── semantic_index_test.cpp           # Tests semantic_index.cpp core functionality
├── symbol_utils_test.cpp             # Tests symbol_utils.cpp
└── integration/                      # Cross-file integration tests
    ├── multifile_definition_test.cpp
    └── multifile_reference_test.cpp
```

### 2. Simple vs Complex Test Fixtures

**Simple fixture for unit tests:**

```cpp
class SimpleTestFixture {
public:
  auto CompileSource(const std::string& code) -> std::unique_ptr<SemanticIndex>;
  auto FindSymbol(const std::string& code, const std::string& name) -> slang::SourceLocation;
  auto GetDefinitionRange(SemanticIndex* index, slang::SourceLocation loc) -> std::optional<slang::SourceRange>;
};
```

**Builder pattern ONLY for multifile integration tests:**

```cpp
// Keep existing MultiFileSemanticFixture with builder pattern
// Only for tests that need opened/unopened file distinctions
auto index = fixture.CreateBuilder()
  .SetCurrentFile(current_code, "current")     // File being edited in LSP
  .AddOpenedFile(opened_code, "opened")        // Another open tab
  .AddUnopendFile(dependency_code, "dep")      // Background dependency
  .Build();
```

### 3. Test Size Guidelines

- **Unit test files**: <300 lines total
- **Individual test cases**: <50 lines each
- **Test focus**: One aspect per test case
- **Test naming**: Descriptive and specific

### 4. Logging Control

**Environment-based logging** (no hardcoded debug levels):

```cpp
auto main(int argc, char* argv[]) -> int {
  // Read from SPDLOG_LEVEL environment variable
  if (auto* level = std::getenv("SPDLOG_LEVEL")) {
    spdlog::set_level(spdlog::level::from_str(level));
  } else {
    spdlog::set_level(spdlog::level::warn);  // Default: quiet
  }
  spdlog::set_pattern("[%l] %v");

  // Suppress Bazel test sharding warnings
  setenv("TEST_SHARD_INDEX", "0", 0);
  setenv("TEST_TOTAL_SHARDS", "1", 0);
  setenv("TEST_SHARD_STATUS_FILE", "", 0);

  return Catch::Session().run(argc, argv);
}
```

**Usage:**

```bash
# Quiet tests (default)
bazel test //test/slangd:semantic_index_test

# Debug logging when needed
SPDLOG_LEVEL=debug bazel test //test/slangd:semantic_index_test

# Info level for development
SPDLOG_LEVEL=info bazel test //test/slangd:semantic_index_test
```

## TDD Workflow Benefits

### Before Refactoring

```
Working on definition_extractor.cpp:
1. Where do I write tests? (unclear)
2. Open semantic_index_basic_test.cpp? (wrong file)
3. Find relevant test among 19 test cases (hard)
4. Add test to 875-line file (overwhelming)
```

### After Refactoring

```
Working on definition_extractor.cpp:
1. Open definition_extractor_test.cpp (obvious)
2. Add focused test case (simple)
3. Use SimpleTestFixture (no complexity)
4. Run with SPDLOG_LEVEL=debug if needed (controlled)
```

## Future Feature Support

This structure naturally supports future LSP features:

**Rename functionality:**

- `test/slangd/semantic/rename_test.cpp`
- Simple fixture for basic rename tests
- Builder pattern for cross-file rename tests

**Semantic tokens:**

- `test/slangd/semantic/semantic_tokens_test.cpp`
- Focus on token classification and ranges
- Integration tests for cross-file token consistency

**Hover information:**

- `test/slangd/semantic/hover_test.cpp`
- Test hover content generation
- Integration tests for cross-file hover resolution

## Migration Principles

1. **Incremental**: Each step must keep all tests passing
2. **Focused**: Split by functionality, not arbitrarily
3. **Practical**: Don't over-engineer, solve real TDD pain points
4. **Maintainable**: Clear naming, simple fixtures, controlled logging
