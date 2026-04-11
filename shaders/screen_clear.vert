#version 450

layout(location = 0) out vec4 fragColor;

vec2 triangleCoords[3] = vec2[](
    vec2( 0.0,  0.0),
    vec2( 1.0,  0.0),
    vec2( 0.0,  1.0)
);

void main() {
    gl_Position = vec4(triangleCoords[gl_VertexIndex], 0.0, 1.0);
    fragColor = vec4(0.2f, 0.2f, 0.3f, 1.0f);
}
