
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace lsp {

template <typename T>
void from_json_optional(
    const nlohmann::json& j, const std::string& key, std::optional<T>& value) {
  if (j.contains(key) && !j[key].is_null()) {
    value = j[key].get<T>();
  } else {
    value = std::nullopt;
  }
}

template <typename T>
void to_json_optional(
    nlohmann::json& j, const std::string& key, const std::optional<T>& value) {
  if (value.has_value()) {
    j[key] = value.value();
  }
}

template <typename T>
void from_json_required(
    const nlohmann::json& j, const std::string& key, T& value) {
  j.at(key).get_to(value);
}

template <typename T>
void to_json_required(
    nlohmann::json& j, const std::string& key, const T& value) {
  j[key] = value;
}

}  // namespace lsp
