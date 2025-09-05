// shaders/sky.vert
#version 460

layout(location = 0) out vec3 fragRayDir;

layout(push_constant) uniform PushConstants {
    mat4 invViewProj;
    vec3 cameraPos;
    float time;
} pc;

void main() {
    // Full-screen triangle
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec4 pos = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    
    vec4 worldPos = pc.invViewProj * pos;
    fragRayDir = normalize(worldPos.xyz / worldPos.w - pc.cameraPos);
    
    gl_Position = pos;
}