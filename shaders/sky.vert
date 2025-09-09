// shaders/sky.vert
#version 460
layout(location = 0) out vec3 fragRayDir;

layout(push_constant) uniform PushConstants {
    mat4 invViewProj;
    vec4 cameraPosAndTime;
} pc;

const vec4 positions[3] = vec4[3](
    vec4(-1.0, -1.0, 1.0, 1.0),
    vec4( 3.0, -1.0, 1.0, 1.0),
    vec4(-1.0,  3.0, 1.0, 1.0)
);

void main() {
    gl_Position = positions[gl_VertexIndex];
    
    vec4 worldPos = pc.invViewProj * gl_Position;
    fragRayDir = worldPos.xyz / worldPos.w - pc.cameraPosAndTime.xyz;
}