#pragma once

#include <optional>
#include <unordered_map>

#include "slang/ast/Compilation.h"
#include "slang/text/SourceLocation.h"

namespace slangd::semantic {

// Uniquely identifies a symbol by its declaration location
struct SymbolKey {
  uint32_t bufferId;
  size_t offset;

  auto operator==(const SymbolKey&) const -> bool = default;
};

}  // namespace slangd::semantic

namespace std {
template <>
struct hash<slangd::semantic::SymbolKey> {
  auto operator()(const slangd::semantic::SymbolKey& key) const -> size_t {
    return std::hash<uint32_t>()(key.bufferId) ^
           (std::hash<uint32_t>()(key.offset) << 1);
  }
};
}  // namespace std

namespace slangd::semantic {

class SymbolIndex {
 public:
  // Factory method to create a SymbolIndex from a compilation
  static auto FromCompilation(slang::ast::Compilation& compilation)
      -> SymbolIndex;

  auto LookupSymbolAt(slang::SourceLocation loc) const
      -> std::optional<SymbolKey>;

  auto GetDefinitionLocation(const SymbolKey& key) const
      -> std::optional<slang::SourceLocation>;

  [[nodiscard]] auto GetDefinitionLocations() const
      -> const std::unordered_map<SymbolKey, slang::SourceLocation>& {
    return definition_locations_;
  }

  [[nodiscard]] auto GetReferenceMap() const
      -> const std::unordered_map<slang::SourceRange, SymbolKey>& {
    return reference_map_;
  }

 private:
  // Maps a symbol key to its declaration location
  std::unordered_map<SymbolKey, slang::SourceLocation> definition_locations_;

  // Maps a source location to a referenced symbol key
  std::unordered_map<slang::SourceRange, SymbolKey> reference_map_;
};

}  // namespace slangd::semantic
