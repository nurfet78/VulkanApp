// shaders/grass.vert
#version 460

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// Instance data
layout(location = 4) in mat4 inInstanceMatrix;
layout(location = 8) in vec4 inInstanceColor;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec4 fragColor;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec3 cameraPos;
    float time;
} pc;

// Wind animation
vec3 applyWind(vec3 pos, vec3 worldPos) {
    float windStrength = 0.3;
    float windFreq = 2.0;
    
    // Only affect upper vertices
    float heightFactor = smoothstep(0.0, 1.0, inTexCoord.y);
    
    // Wave pattern
    float wave = sin(worldPos.x * 0.5 + pc.time * windFreq) * 
                 cos(worldPos.z * 0.3 + pc.time * windFreq * 0.7);
    
    pos.x += wave * windStrength * heightFactor;
    pos.z += wave * windStrength * heightFactor * 0.5;
    
    return pos;
}

void main() {
    mat4 modelMatrix = inInstanceMatrix;
    vec3 localPos = applyWind(inPosition, (modelMatrix * vec4(0, 0, 0, 1)).xyz);
    vec4 worldPos = modelMatrix * vec4(localPos, 1.0);
    
    fragWorldPos = worldPos.xyz;
    fragNormal = normalize(mat3(transpose(inverse(modelMatrix))) * inNormal);
    fragTexCoord = inTexCoord;
    fragColor = inInstanceColor;
    
    gl_Position = pc.viewProj * worldPos;
}