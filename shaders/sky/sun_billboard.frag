#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform sampler2D sunTexture;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec3 sunDirection;
    float sunSize;
    vec3 cameraPos;
    float intensity;
} push;

void main() {
    vec4 sunColor = texture(sunTexture, inUV);
    
    // Apply intensity
    sunColor.rgb *= push.intensity;
    
    // Output with alpha for additive blending
    fragColor = sunColor;
}