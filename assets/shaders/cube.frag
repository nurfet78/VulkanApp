#version 450

// Входы из вершинного шейдера
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;

// Выход
layout(location = 0) out vec4 outColor;

// Структуры данных из C++
struct LightData {
    vec3 position;
    float range;
    vec3 direction;
    float intensity;
    vec3 color;
    int type;
    vec2 coneAngles;
};

// Uniform-блок материала
layout(set = 1, binding = 0) uniform MaterialUBO {
    vec4 baseColor;
    float metallic;
    float roughness;
    float ao;
    float emissive;
} material;

// Uniform-блок сцены
layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float time;
    LightData lights[8];
    int lightCount;
    vec3 ambientColor;
} ubo;

// НОВОЕ: Skybox cubemap для IBL
layout(set = 0, binding = 1) uniform samplerCube skyboxCubemap;

const float PI = 3.14159265359;
const float EPSILON = 0.001;

// --- PBR-функции ---
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return num / max(denom, EPSILON);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return num / max(denom, EPSILON);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ----------------------------------------------------------------------------

void main() {
    // Проверка корректности нормалей
    if (length(fragNormal) < 0.1) {
        outColor = vec4(1.0, 0.0, 0.0, 1.0);
        return;
    }
    
    // Получение свойств материала
    vec3 albedo = material.baseColor.rgb;
    float metallic = clamp(material.metallic, 0.0, 1.0);
    float roughness = clamp(material.roughness, 0.04, 1.0);
    
    // Векторы
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(ubo.cameraPos - fragWorldPos);
    float NdotV = max(dot(N, V), EPSILON);
    
    // Fresnel reflectance at normal incidence
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);
    
    // ========================================================================
    // IBL (Image-Based Lighting) - НОВОЕ!
    // ========================================================================
    
    // Reflection vector для зеркальных отражений
    vec3 R = reflect(-V, N);
    
    // Fresnel для IBL
    vec3 F_ibl = FresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kS_ibl = F_ibl;
    vec3 kD_ibl = (1.0 - kS_ibl) * (1.0 - metallic);
    
    // Diffuse IBL - сэмплируем окружение с нормалью
    vec3 irradiance = texture(skyboxCubemap, N).rgb;
    vec3 diffuse_ibl = irradiance * albedo;
    
    // Specular IBL - сэмплируем отражение с учётом roughness
    // Простой метод: используем mip-уровни для размытия (если есть)
    float mipLevel = roughness * 4.0; // Предполагаем 5 mip-уровней (0-4)
    vec3 prefilteredColor = textureLod(skyboxCubemap, R, mipLevel).rgb;
    
    // Specular BRDF integration (упрощенная версия без LUT)
    vec2 envBRDF = vec2(1.0 - roughness, 1.0); // Упрощение
    vec3 specular_ibl = prefilteredColor * (F_ibl * envBRDF.x + envBRDF.y);
    
    // Ambient от IBL
    vec3 ambient = (kD_ibl * diffuse_ibl + specular_ibl) * material.ao;
    
    // ========================================================================
    // Direct Lighting - источники света
    // ========================================================================
    
    vec3 Lo = ambient;
    
    // Цикл по всем источникам света
    for (int i = 0; i < ubo.lightCount; i++) {
        LightData light = ubo.lights[i];
        
        vec3 L;
        float attenuation = 1.0;
        
        // Расчет направления света и затухания
        if (light.type == 0) {
            // Directional light
            L = normalize(-light.direction);
        } else {
            // Point/Spot light
            vec3 lightVec = light.position - fragWorldPos;
            float distance = length(lightVec);
            L = normalize(lightVec);
            
            // Smooth attenuation
            attenuation = 1.0 - smoothstep(light.range * 0.75, light.range, distance);
            attenuation *= attenuation;
            
            // Spot light cone
            if (light.type == 2) {
                float theta = dot(L, normalize(-light.direction));
                float outerCone = cos(radians(light.coneAngles.y));
                float innerCone = cos(radians(light.coneAngles.x));
                attenuation *= smoothstep(outerCone, innerCone, theta);
            }
        }
        
        // Half vector
        vec3 H = normalize(V + L);
        
        // Важные dot products
        float NdotL = max(dot(N, L), 0.0);
        float HdotV = max(dot(H, V), 0.0);
        
        // Пропускаем если свет не попадает на поверхность
        if (NdotL <= 0.0) continue;
        
        // Radiance
        vec3 radiance = light.color * light.intensity * attenuation;
        
        // Cook-Torrance BRDF
        float NDF = DistributionGGX(N, H, roughness);
        float G   = GeometrySmith(N, V, L, roughness);
        vec3  F   = FresnelSchlick(HdotV, F0);
        
        // Расчет specular
        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * NdotV * NdotL;
        vec3 specular = numerator / max(denominator, EPSILON);
        
        // Энергосбережение
        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
        
        // Добавляем вклад света
        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }
    
    // Emissive
    Lo += albedo * material.emissive;
    
    // Проверка на NaN и бесконечность
    if (any(isnan(Lo)) || any(isinf(Lo))) {
        Lo = vec3(0.0);
    }
    
    // Clamp перед tone mapping
    Lo = clamp(Lo, 0.0, 100.0);
    
    // Tone mapping (Reinhard)
    Lo = Lo / (Lo + vec3(1.0));
    
    // Gamma correction
    Lo = pow(Lo, vec3(1.0/2.2));
    
    outColor = vec4(Lo, 1.0);
}