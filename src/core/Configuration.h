#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <type_traits>

namespace core {

class Configuration {
  public:
    static Configuration &get_instance();
    static nlohmann::json &get_root();

    Configuration(const Configuration &) = delete;
    Configuration &operator=(const Configuration &) = delete;

    bool load(const std::string &path = "assets/scenes/configuration.json");
    bool is_loaded() const { return loaded; }

    // Search the loaded JSON for a value by key. Checks the root object first,
    // then recursively matches objects whose "name" field equals key (e.g.
    // validationFeatures entries). Returns a default-constructed T if not found
    // or not convertible.
    template <typename T>
    T find(const std::string &key) const {
        if (!loaded) {
            return T{};
        }

        if (root.contains(key)) {
            return try_get<T>(root[key]);
        }

        const nlohmann::json *node = find_node(root, key);
        if (!node) {
            return T{};
        }

        return try_get<T>(*node);
    }

  private:
    static const nlohmann::json *find_node(const nlohmann::json &node,
                                           const std::string &key);
    template <typename T>
    static T try_get(const nlohmann::json &node);

    Configuration() = default;

    nlohmann::json root;
    bool loaded{false};
};

template <typename T>
T Configuration::try_get(const nlohmann::json &node) {
    try {
        if constexpr (std::is_same_v<T, bool>) {
            if (node.is_boolean()) {
                return node.get<bool>();
            }
            if (node.is_object() && node.contains("enabled")) {
                return node["enabled"].get<bool>();
            }
        } else if constexpr (std::is_same_v<T, std::string>) {
            if (node.is_string()) {
                return node.get<std::string>();
            }
            if (node.is_object() && node.contains("value")) {
                return node["value"].get<std::string>();
            }
        }
        return node.get<T>();
    } catch (...) {
        return T{};
    }
}

} // namespace core