// atmosphere.vert - Fullscreen triangle vertex shader
#version 450 core

layout(location = 0) out vec2 uv;

void main() {
    // Generate fullscreen triangle
    uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}