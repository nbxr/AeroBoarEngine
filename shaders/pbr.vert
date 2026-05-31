#version 450

// Push constants (Phase 1)
layout(push_constant) uniform PushConstants {
    mat4   viewProj;
    mat4   model;
    uvec4  extra;      // x = materialIndex
    vec4   cameraPos;  // xyz = world-space camera (w unused) — Phase 1 lighting
} pc;

// Vertex attributes (proper input — replaces legacy SSBO pulling)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
// location 3 holds the packed UV0 bits in .xy (first 4 bytes of the uv[8] field in gfx::Vertex)
layout(location = 3) in vec2 inUVPacked;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec4 outTangent;
layout(location = 3) out vec2 outUV;

void main() {
    // Unpack UV0 (stored as two uint16 packed into the first 4 bytes)
    // We treat the incoming vec2 bits exactly as the old float-based pulling did.
    uint uvPacked = floatBitsToUint(inUVPacked.x);
    vec2 uv;
    uv.x = float((uvPacked >> 0) & 0xFFFFu) / 65535.0;
    uv.y = float((uvPacked >> 16) & 0xFFFFu) / 65535.0;

    // NOTE: We intentionally do NOT flip V here.
    //
    // glTF 2.0 defines UV origin at the top-left of the image (U right, V down).
    // stb_image also loads images with (0,0) at top-left.
    // When uploading directly to Vulkan, this matches the image data layout.
    // Adding a 1.0 - uv.y flip was causing textures to sample from the bottom
    // of the image (upside-down appearance) on correctly authored glTF assets.

    // Use the model matrix pushed per draw
    mat4 modelMat = pc.model;

    vec4 worldPos = modelMat * vec4(inPosition, 1.0);

    outWorldPos = worldPos.xyz;
    outNormal   = normalize(mat3(modelMat) * inNormal);
    outTangent  = vec4(normalize(mat3(modelMat) * inTangent.xyz), inTangent.w);
    outUV       = uv;

    gl_Position = pc.viewProj * worldPos;
}