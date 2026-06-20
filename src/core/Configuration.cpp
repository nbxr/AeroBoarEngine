#include "core/Configuration.h"
#include "core/Log.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace core {

Configuration& Configuration::get_instance() {
    static Configuration instance;
    return instance;
}

nlohmann::json& Configuration::get_root() {
    return core::Configuration::get_instance().root;
}

const nlohmann::json *Configuration::find_node(const nlohmann::json &node,
                                               const std::string &key) {
    if (node.is_object()) {
        if (node.contains("name") && node["name"].is_string() &&
            node["name"].get<std::string>() == key) {
            return &node;
        }

        for (const auto &[_, value] : node.items()) {
            if (const nlohmann::json *found = find_node(value, key)) {
                return found;
            }
        }
    } else if (node.is_array()) {
        for (const auto &element : node) {
            if (const nlohmann::json *found = find_node(element, key)) {
                return found;
            }
        }
    }

    return nullptr;
}

bool Configuration::load(const std::string& path) {
    try {
        std::ifstream config_file(path);
        if (!config_file.is_open()) {
            LOG_ERROR("Failed to open configuration file: " << path);
            return false;
        }

        root = nlohmann::json::parse(config_file);

        loaded = true;
        LOG_INFO("[Config] Loaded " << path);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Error loading configuration: " << e.what());
        loaded = false;
        return false;
    }
}

} // namespace core