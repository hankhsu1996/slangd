#include "slangd/utils/canonical_path.hpp"

#include "slangd/utils/path_utils.hpp"

namespace slangd {

CanonicalPath::CanonicalPath(std::filesystem::path path)
    : path_(NormalizePath(std::move(path))) {
}

auto CanonicalPath::FromUri(std::string_view uri) -> CanonicalPath {
  return CanonicalPath(UriToPath(uri));
}

auto CanonicalPath::CurrentPath() -> CanonicalPath {
  return CanonicalPath(std::filesystem::current_path());
}

auto CanonicalPath::ToUri() const -> std::string {
  return PathToUri(path_);
}

auto CanonicalPath::Path() const -> const std::filesystem::path& {
  return path_;
}

auto CanonicalPath::String() const -> const std::string& {
  if (cached_string_.empty()) {
    cached_string_ = path_.string();
  }
  return cached_string_;
}

auto CanonicalPath::Empty() const -> bool {
  return path_.empty();
}

auto CanonicalPath::IsSubPathOf(const CanonicalPath& other) const -> bool {
  return std::mismatch(other.path_.begin(), other.path_.end(), path_.begin())
             .first == other.path_.end();
}

auto CanonicalPath::operator/(std::filesystem::path rhs) const
    -> CanonicalPath {
  return CanonicalPath(path_ / rhs);
}

}  // namespace slangd
