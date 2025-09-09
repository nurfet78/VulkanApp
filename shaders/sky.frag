// shaders/sky.frag
#version 460

layout(location = 0) in vec3 fragRayDir;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 invViewProj;
    vec4 cameraPosAndTime; // Единое поле для позиции и времени
} pc;

// Procedural sky gradient
// Теперь функция должна принимать время как аргумент, чтобы быть "чистой"
vec3 getSkyColor(vec3 rayDir, float time) {
    float y = rayDir.y * 0.5 + 0.5;
    
    vec3 horizonColor = vec3(0.7, 0.8, 0.9);
    vec3 zenithColor = vec3(0.4, 0.6, 0.9);
    vec3 groundColor = vec3(0.3, 0.35, 0.4);
    
    vec3 skyColor;
    if (rayDir.y > 0.0) {
        skyColor = mix(horizonColor, zenithColor, pow(y, 0.5));
    } else {
        skyColor = mix(horizonColor, groundColor, pow(-rayDir.y, 0.5));
    }
    
    // Add sun
    vec3 sunDir = normalize(vec3(-0.5, -0.3, -0.5));
    float sun = pow(max(dot(rayDir, -sunDir), 0.0), 256.0);
    skyColor += vec3(1.0, 0.9, 0.7) * sun;
    
    // Simple clouds
    // --- ИСПРАВЛЕНИЕ ЗДЕСЬ ---
    // Используем 'time', переданный как аргумент, а не глобальный pc.time
    float cloud = sin(rayDir.x * 10.0 + time * 0.1) * 
                  cos(rayDir.z * 8.0 + time * 0.05);
    // -------------------------
    cloud = smoothstep(0.3, 0.7, cloud) * 0.1;
    skyColor = mix(skyColor, vec3(1.0), cloud * smoothstep(0.0, 0.3, rayDir.y));
    
    return skyColor; // <-- Убедимся, что возвращается vec3
}

void main() {
    // 1. Извлекаем время из push-константы
    float currentTime = pc.cameraPosAndTime.w;
    
    // 2. Вызываем getSkyColor с обоими аргументами
    vec3 color = getSkyColor(normalize(fragRayDir), currentTime);
    
    outColor = vec4(color, 1.0);
}