#version 450

layout(location = 0) out vec2 outUV;

layout(location = 1) out vec3 debugSunDir; // DEBUG

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
    debugSunDir = atmosphere.sunDirection; // DEBUG
    
    // ВАЖНО: проверяем что sunDirection не нулевой
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