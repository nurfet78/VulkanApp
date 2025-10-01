#version 450

// Vertex attributes
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;

// Push constants — модель и нормали отдельно
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 normalMatrix;
} pc;

struct LightData {
    vec3 position;
    float range;
    vec3 direction;
    float intensity;
    vec3 color;
    int type;
    vec2 coneAngles;
    int shadingModel;
    float wrapFactor;
    int toonSteps;
    float softness;
    float padding;
};

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float time;
    LightData lights[8];
    int lightCount;
    vec3 ambientColor;
    float padding;
} ubo;

// Outputs to fragment shader
layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragViewPos;
layout(location = 4) out float fragTime;
layout(location = 5) out vec3 fragTangent;

void main() {
    // ИСПРАВЛЕНО: World position используя ТОЛЬКО модельную матрицу
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    
    // Normal (уже правильно)
    fragNormal = normalize((pc.normalMatrix * vec4(inNormal, 0.0)).xyz);
    
    // Tangent
    fragTangent = normalize((pc.normalMatrix * vec4(inTangent, 0.0)).xyz);
    
    // Texture coordinates
    fragTexCoord = inTexCoord;
    
    // Camera position
    fragViewPos = ubo.cameraPos;
    
    // Time
    fragTime = ubo.time;
    
    // ИСПРАВЛЕНО: Final position = Projection * View * World
    gl_Position = ubo.projection * ubo.view * worldPos;
    gl_Position.y = -gl_Position.y;
}