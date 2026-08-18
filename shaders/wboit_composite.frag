#version 450

layout(set = 0, binding = 0) uniform sampler2D accumTex;
layout(set = 0, binding = 1) uniform sampler2D revealTex;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

void main() {
    vec4 accum = texture(accumTex, inUV);
    float reveal = texture(revealTex, inUV).r; // Π(1-α)
    vec3 avg = accum.rgb / max(accum.a, 1e-5);
    avg = avg / (avg + 1.0); // match opaque Reinhard
    float coverage = 1.0 - clamp(reveal, 0.0, 1.0);
    outColor = vec4(avg, coverage);
}
