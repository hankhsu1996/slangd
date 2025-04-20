#pragma once

#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "slang/ast/Compilation.h"
#include "slang/text/SourceLocation.h"

namespace slangd::semantic {

// Uniquely identifies a symbol by its declaration location
struct SymbolKey {
  uint32_t bufferId;
  size_t offset;

  auto operator==(const SymbolKey&) const -> bool = default;

  // Factory method to create from SourceLocation
  static auto FromSourceLocation(const slang::SourceLocation& loc)
      -> SymbolKey {
    return SymbolKey{.bufferId = loc.buffer().getId(), .offset = loc.offset()};
  }
};

}  // namespace slangd::semantic

namespace std {
template <>
struct hash<slangd::semantic::SymbolKey> {
  auto operator()(const slangd::semantic::SymbolKey& key) const -> size_t {
    size_t hash = std::hash<uint32_t>()(key.bufferId);
    hash ^= std::hash<size_t>()(key.offset) + 0x9e3779b9 + (hash << 6) +
            (hash >> 2);
    return hash;
  }
};
}  // namespace std

namespace slangd::semantic {

class SymbolIndex {
 public:
  SymbolIndex() = delete;

  explicit SymbolIndex(slang::ast::Compilation& compilation)
      : compilation_(compilation) {
  }

  // Create a symbol index from a compilation
  static auto FromCompilation(
      slang::ast::Compilation& compilation,
      const std::unordered_set<std::string>& traverse_paths = {})
      -> SymbolIndex;

  // Looks up a symbol at the given location
  auto LookupSymbolAt(slang::SourceLocation loc) const
      -> std::optional<SymbolKey>;

  // Gets the definition range for a symbol
  auto GetDefinitionRange(const SymbolKey& key) const
      -> std::optional<slang::SourceRange>;

  [[nodiscard]] auto GetDefinitionRanges() const
      -> const std::unordered_map<SymbolKey, slang::SourceRange>& {
    return definition_locations_;
  }

  [[nodiscard]] auto GetReferenceMap() const
      -> const std::unordered_map<slang::SourceRange, SymbolKey>& {
    return reference_map_;
  }

  // Adds a definition location for a symbol
  // This will also add the symbol to the reference map
  void AddDefinition(const SymbolKey& key, const slang::SourceRange& range);

  // Adds a reference location for a symbol
  void AddReference(const slang::SourceRange& range, const SymbolKey& key);

 private:
  // Store the compilation reference
  std::reference_wrapper<slang::ast::Compilation> compilation_;

  // Maps a symbol key to its declaration range
  std::unordered_map<SymbolKey, slang::SourceRange> definition_locations_;

  // Maps a source range to a referenced symbol key
  std::unordered_map<slang::SourceRange, SymbolKey> reference_map_;
};

}  // namespace slangd::semantic
