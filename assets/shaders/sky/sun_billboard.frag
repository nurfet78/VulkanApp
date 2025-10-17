#version 450 core

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 fragColor;

// Binding 0: текстура солнца (если используется)
layout(set = 0, binding = 0) uniform sampler2D sunTexture;

// Binding 1: Atmosphere UBO (ИЗМЕНЕНО с 0 на 1!)
layout(set = 0, binding = 1, std140) uniform AtmosphereUniforms {
    vec3 sunDirection;   // offset = 0
    float sunIntensity;  // offset = 12

    vec3 rayleighBeta;   // offset = 16
    vec3 mieBeta;        // offset = 32
    float mieG;          // offset = 44

    float turbidity;     // offset = 48
    float planetRadius;  // offset = 52
    float atmosphereRadius; // offset = 56
    float time;          // offset = 60

    vec3 cameraPos;      // offset = 64
    float exposure;      // offset = 76
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
    
    // Реалистичное солнце с limb darkening
    float mu = sqrt(max(0.0, 1.0 - dist * dist));
    float limbDarkening = 0.4 + 0.6 * mu;
    
    vec3 sunColor = vec3(1.0, 0.98, 0.92);
    
    // Мягкий край
    float edgeSoftness = 1.0 - smoothstep(0.96, 1.0, dist);
    
    float brightness = limbDarkening * edgeSoftness;
    
    // Применяем интенсивность из UBO
    sunColor *= brightness * atmosphere.sunIntensity * 0.15;
    
    fragColor = vec4(sunColor, 1.0);
}