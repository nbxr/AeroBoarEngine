#include "gfx/ShaderLoader.h"
#include <fstream>
#include <stdexcept>
#include <vector>
#include <iostream>
#include <sstream>
#include <cstring>

bool gfx::load_shader_source(const std::string& filename, std::vector<unsigned int>& out_code) {
    // This is a simplified implementation - in production you'd use glslangValidator or similar
    // For now, we'll compile GLSL to SPIR-V at runtime using a shader compiler
    // Since we can't easily do that without external dependencies, let's create
    // a minimal working solution by reading pre-compiled SPIR-V files
    
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Failed to open shader file: " << filename << std::endl;
        return false;
    }
    
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    out_code.resize(file_size / sizeof(unsigned int));
    file.read(reinterpret_cast<char*>(out_code.data()), file_size);
    file.close();
    
    return true;
}
