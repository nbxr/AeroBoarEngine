#pragma once
#include <vector>
#include <string>

namespace gfx {
bool load_shader_source(const std::string& filename, std::vector<unsigned int>& out_code);
}
