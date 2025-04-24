#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include <fmt/format.h>

namespace slangd {

class CanonicalPath {
 public:
  CanonicalPath() = default;

  explicit CanonicalPath(std::filesystem::path path);

  static auto FromUri(std::string_view uri) -> CanonicalPath;
  static auto CurrentPath() -> CanonicalPath;

  auto ToUri() const -> std::string;

  auto Path() const -> const std::filesystem::path&;
  auto String() const -> const std::string&;

  auto Empty() const -> bool;

  auto IsSubPathOf(const CanonicalPath& other) const -> bool;

  explicit operator std::string() const {
    return String();
  }

  friend auto operator==(const CanonicalPath& lhs, const CanonicalPath& rhs)
      -> bool {
    return lhs.String() == rhs.String();
  }

  friend auto operator!=(const CanonicalPath& lhs, const CanonicalPath& rhs)
      -> bool {
    return !(lhs == rhs);
  }

  friend auto operator<(const CanonicalPath& lhs, const CanonicalPath& rhs)
      -> bool {
    return lhs.String() < rhs.String();
  }

  auto operator/(std::filesystem::path rhs) const -> CanonicalPath;

 private:
  std::filesystem::path path_;
  mutable std::string cached_string_;
};

}  // namespace slangd

// Format support for logging
template <>
struct fmt::formatter<slangd::CanonicalPath> : fmt::formatter<std::string> {
  template <typename FormatContext>
  auto format(const slangd::CanonicalPath& p, FormatContext& ctx) const {
    return fmt::formatter<std::string>::format(p.String(), ctx);
  }
};

// Hash support for unordered_map / unordered_set
template <>
struct std::hash<slangd::CanonicalPath> {
  auto operator()(const slangd::CanonicalPath& path) const noexcept
      -> std::size_t {
    return std::hash<std::string>{}(path.String());
  }
};
