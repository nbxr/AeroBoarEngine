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
        debug = {};
        if (root.contains("debug") && root["debug"].is_object()) {
            const auto& d = root["debug"];
            if (d.contains("exitAfterFrames") && d["exitAfterFrames"].is_number_integer())
                debug.exit_after_frames = d["exitAfterFrames"].get<int>();
            if (d.contains("hiddenWindow") && d["hiddenWindow"].is_boolean())
                debug.hidden_window = d["hiddenWindow"].get<bool>();
            if (d.contains("clearLogOnStart") && d["clearLogOnStart"].is_boolean())
                debug.clear_log_on_start = d["clearLogOnStart"].get<bool>();
            if (d.contains("verbose") && d["verbose"].is_boolean())
                debug.verbose = d["verbose"].get<bool>();
            if (d.contains("logCull") && d["logCull"].is_boolean())
                debug.log_cull = d["logCull"].get<bool>();
            if (d.contains("queenShadowProbe") && d["queenShadowProbe"].is_boolean())
                debug.queen_shadow_probe = d["queenShadowProbe"].get<bool>();
            if (d.contains("shadowMapDump") && d["shadowMapDump"].is_string())
                debug.shadow_map_dump = d["shadowMapDump"].get<std::string>();
            if (d.contains("shadowProbeTarget") && d["shadowProbeTarget"].is_string())
                debug.shadow_probe_target = d["shadowProbeTarget"].get<std::string>();
        }
        core::g_log_verbose = debug.verbose;
        core::g_log_cull = debug.log_cull;
        LOG_INFO("[Config] Loaded " << path);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Error loading configuration: " << e.what());
        loaded = false;
        return false;
    }
}

} // namespace core