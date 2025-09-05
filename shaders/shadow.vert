// shaders/shadow.vert
#version 460

layout(location = 0) in vec3 inPosition;

// Instance data for instanced shadows
layout(location = 4) in mat4 inInstanceMatrix;

layout(push_constant) uniform PushConstants {
    mat4 lightViewProj;
} pc;

void main() {
    mat4 modelMatrix = gl_InstanceIndex > 0 ? inInstanceMatrix : mat4(1.0);
    gl_Position = pc.lightViewProj * modelMatrix * vec4(inPosition, 1.0);
}