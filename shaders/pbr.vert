// shaders/pbr.vert
#version 460
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;

// Instance attributes
layout(location = 4) in mat4 inInstanceMatrix;
layout(location = 8) in vec4 inInstanceColor;
layout(location = 9) in uint inTextureIndex;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragTangent;
layout(location = 4) out vec3 fragBitangent;
layout(location = 5) out vec4 fragColor;
layout(location = 6) flat out uint fragTextureIndex;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec3 cameraPos;
    float time;
} pc;

void main() {
    mat4 modelMatrix = gl_InstanceIndex > 0 ? inInstanceMatrix : mat4(1.0);
    vec4 worldPos = modelMatrix * vec4(inPosition, 1.0);
    
    fragWorldPos = worldPos.xyz;
    fragNormal = normalize(mat3(transpose(inverse(modelMatrix))) * inNormal);
    fragTangent = normalize(mat3(modelMatrix) * inTangent);
    fragBitangent = cross(fragNormal, fragTangent);
    fragTexCoord = inTexCoord;
    fragColor = inInstanceColor;
    fragTextureIndex = inTextureIndex;
    
    gl_Position = pc.viewProj * worldPos;
}