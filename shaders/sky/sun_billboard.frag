#version 450 core

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 fragColor;


layout(binding = 0) uniform sampler2D sunTexture;

layout(set = 0, binding = 1) uniform AtmosphereUniforms {
    vec3 sunDirection;
    float sunIntensity;
    vec3 rayleighBeta;
    float mieCoeff;
    vec3 mieBeta;
    float mieG;
    float turbidity;
    float planetRadius;
    float atmosphereRadius;
    float time;
    vec3 cameraPos;
    float exposure;
} atmosphere;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    float sunSize;
    vec3 cameraPos;
    float aspectRatio;
} push;

void main() {

   vec2 center = inUV * 2.0 - 1.0;
    float dist = length(center);
    
    if (dist > 1.0) {
        discard;
    }
    
    // ПРОСТОЙ ТЕСТ: яркий белый круг
    float brightness = 1.0 - dist;
    vec3 color = vec3(1.0, 0.98, 0.9) * brightness;
    
    fragColor = vec4(color, 1.0); // Без умножения на intensity
}
