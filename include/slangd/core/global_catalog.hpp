#pragma once

#include <string>
#include <vector>

#include "slangd/utils/canonical_path.hpp"

namespace slangd {

// Forward declaration of package/interface metadata structures
// These will be implemented in Phase 2 when GlobalCatalog is populated
struct PackageInfo {
  std::string name;
  CanonicalPath file_path;
  // Future: additional metadata
};

struct InterfaceInfo {
  std::string name;
  CanonicalPath file_path;
  // Future: additional metadata
};

// Empty interface for Global Catalog - designed for future implementation
// Phase 2 will populate this with actual compilation metadata
// For now, OverlaySession can accept nullptr and work in single-file mode
class GlobalCatalog {
 public:
  GlobalCatalog() = default;
  GlobalCatalog(const GlobalCatalog&) = delete;
  GlobalCatalog(GlobalCatalog&&) = delete;
  auto operator=(const GlobalCatalog&) -> GlobalCatalog& = delete;
  auto operator=(GlobalCatalog&&) -> GlobalCatalog& = delete;
  virtual ~GlobalCatalog() = default;

  // Future Phase 2 interface - for now returns empty
  [[nodiscard]] virtual auto GetPackages() const
      -> const std::vector<PackageInfo>& {
    static const std::vector<PackageInfo> kEmpty;
    return kEmpty;
  }

  [[nodiscard]] virtual auto GetInterfaces() const
      -> const std::vector<InterfaceInfo>& {
    static const std::vector<InterfaceInfo> kEmpty;
    return kEmpty;
  }

  [[nodiscard]] virtual auto GetIncludeDirectories() const
      -> const std::vector<CanonicalPath>& {
    static const std::vector<CanonicalPath> kEmpty;
    return kEmpty;
  }

  [[nodiscard]] virtual auto GetDefines() const
      -> const std::vector<std::string>& {
    static const std::vector<std::string> kEmpty;
    return kEmpty;
  }

  // Version tracking for future atomic snapshots
  [[nodiscard]] virtual auto GetVersion() const -> uint64_t {
    return 0;
  }
};

}  // namespace slangd
