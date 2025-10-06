#pragma once

#include <string>
#include <vector>

#include "slangd/utils/canonical_path.hpp"

namespace slangd {

// ProjectLayout represents the final authoritative view of a project's
// configuration and discovered files. This is the data structure that
// downstream services consume (primarily GlobalCatalog for cross-file
// compilation and OverlaySession for file resolution).
class ProjectLayout {
 public:
  // Constructors
  ProjectLayout() = default;

  ProjectLayout(
      std::vector<CanonicalPath> files, std::vector<CanonicalPath> include_dirs,
      std::vector<std::string> defines)
      : files_(std::move(files)),
        include_dirs_(std::move(include_dirs)),
        defines_(std::move(defines)) {
  }

  // Move and copy constructors/assignment
  ProjectLayout(const ProjectLayout&) = default;
  auto operator=(const ProjectLayout&) -> ProjectLayout& = default;
  ProjectLayout(ProjectLayout&&) = default;
  auto operator=(ProjectLayout&&) -> ProjectLayout& = default;

  // Destructor
  ~ProjectLayout() = default;
  // Accessors for project data
  [[nodiscard]] auto GetFiles() const -> const std::vector<CanonicalPath>& {
    return files_;
  }

  [[nodiscard]] auto GetIncludeDirs() const
      -> const std::vector<CanonicalPath>& {
    return include_dirs_;
  }

  [[nodiscard]] auto GetDefines() const -> const std::vector<std::string>& {
    return defines_;
  }

 private:
  // Source files discovered from config or auto-discovery
  std::vector<CanonicalPath> files_;

  // Include directories for `include files
  std::vector<CanonicalPath> include_dirs_;

  // Macro definitions (NAME or NAME=value)
  std::vector<std::string> defines_;
};

}  // namespace slangd
