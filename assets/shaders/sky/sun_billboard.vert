#version 450

#extension GL_KHR_vulkan_glsl : enable

layout(location = 0) out vec2 outUV;

// Binding 1 для Atmosphere UBO
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
    vec2 quadVertices[6] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 1.0, -1.0),
        vec2( 1.0,  1.0),
        vec2(-1.0, -1.0),
        vec2( 1.0,  1.0),
        vec2(-1.0,  1.0)
    );
    
    vec2 localPos = quadVertices[gl_VertexIndex];
    outUV = localPos * 0.5 + 0.5;
    
    vec3 sunDir = normalize(atmosphere.sunDirection);
    
    float sunDistance = 100000.0;
    vec3 sunWorldPos = push.cameraPos + sunDir * sunDistance;
    
    vec3 forward = normalize(push.cameraPos - sunWorldPos);
    vec3 worldUp = vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(worldUp, forward));
    vec3 up = cross(forward, right);
    
    float billboardWorldSize = push.sunSize * sunDistance * 0.01;
    
    vec3 vertexWorldPos = sunWorldPos 
                        + right * localPos.x * billboardWorldSize
                        + up * localPos.y * billboardWorldSize;
    
    gl_Position = push.viewProj * vec4(vertexWorldPos, 1.0);
}