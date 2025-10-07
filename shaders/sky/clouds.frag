// clouds_optimized.frag Ч Volumetric cloud shader (optimized & stable)
// © Ќури, 2025

#version 450 core

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform CloudUniforms {
    vec3 coverage;
    float speed;
    vec3 windDirection;
    float scale;
    float density;
    float altitude;
    float thickness;
    float time;
    int octaves;
    float lacunarity;
    float gain;
    float _pad;
} clouds;

layout(binding = 1) uniform sampler3D cloudNoise;
layout(binding = 2) uniform sampler3D cloudDetail;

layout(push_constant) uniform CloudPushConstants {
    mat4 invViewProj;
    vec3 cameraPos;
    float time;
    vec3 sunDirection;
    float coverage;
    vec3 windDirection;
    float cloudScale;
} push;

const int CLOUD_STEPS = 32;
const int LIGHT_STEPS = 4;
const float CLOUD_TOP = 2000.0;
const float CLOUD_BOTTOM = 500.0;

// --- Simplified FBM ---
float fbm_low(vec3 pos)
{
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    // до 4 октав Ч оптимальный баланс
    for (int i = 0; i < 4; ++i) {
        value += amplitude * (texture(cloudNoise, pos * frequency).r * 2.0 - 1.0);
        amplitude *= clouds.gain;
        frequency *= clouds.lacunarity;
    }
    return value * 0.5 + 0.5;
}

// --- Cloud density ---
float cloudDensityFast(vec3 pos)
{
    // ѕрив€зка к миру (чтобы не "искрило")
    vec3 animated = pos * 0.001 + push.windDirection * (push.time * clouds.speed);

    // Ѕазовый шум
    float base = fbm_low(animated * (clouds.scale * 0.0003));

    // ƒетали Ч слабее и реже
    float detail = texture(cloudDetail, animated * (clouds.scale * 0.006)).r;
    base = mix(base, base * detail, 0.4);

    // Coverage Ч м€гкий диапазон, убирает "звЄзды"
    float cov = smoothstep(0.4 - push.coverage * 0.4, 0.9, base);

    // ¬ысотный градиент
    float h = pos.y;
    float hGrad = smoothstep(CLOUD_BOTTOM, CLOUD_BOTTOM + 500.0, h) *
                 (1.0 - smoothstep(CLOUD_TOP - 500.0, CLOUD_TOP, h));

    // ќбща€ плотность
    float d = cov * hGrad * clouds.density;
    d = max(0.0, d - 0.05); // фильтруем мелкий шум

    return clamp(d, 0.0, 1.0);
}

// --- Ѕыстрое освещение по высоте ---
float simpleLightFactor(vec3 pos)
{
    float heightFactor = clamp((pos.y - CLOUD_BOTTOM) / (CLOUD_TOP - CLOUD_BOTTOM), 0.0, 1.0);
    return mix(0.4, 1.0, heightFactor);
}

// --- Ray intersection with cloud layer ---
vec2 rayCloudIntersect(vec3 rayStart, vec3 rayDir)
{
    float t1 = (CLOUD_BOTTOM - rayStart.y) / rayDir.y;
    float t2 = (CLOUD_TOP - rayStart.y) / rayDir.y;

    if (t1 > t2) {
        float tmp = t1; t1 = t2; t2 = tmp;
    }
    if (t2 < 0.0) return vec2(-1.0);

    return vec2(max(t1, 0.0), t2);
}

void main()
{
    // --- Reconstruct world-space ray ---
    vec4 target = push.invViewProj * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec3 rayDir = normalize(target.xyz / target.w - push.cameraPos);
    vec3 rayStart = push.cameraPos;

    // --- Intersect with cloud layer ---
    vec2 cloudIntersect = rayCloudIntersect(rayStart, rayDir);
    if (cloudIntersect.x < 0.0) {
        fragColor = vec4(0.0);
        return;
    }

    float rayLength = cloudIntersect.y - cloudIntersect.x;
    float stepSize = rayLength / float(CLOUD_STEPS);

    vec3 color = vec3(0.0);
    float transmittance = 1.0;
    float accumulated = 0.0;

    int emptyCount = 0;

    // --- March through cloud volume ---
    for (int i = 0; i < CLOUD_STEPS; ++i) {
        if (transmittance < 0.01) break;

        float t = cloudIntersect.x + (float(i) + 0.5) * stepSize;
        vec3 pos = rayStart + rayDir * t;

        float d = cloudDensityFast(pos);

        if (d < 0.001) {
            emptyCount++;
            if (emptyCount > 4) continue;
        } else emptyCount = 0;

        float lightT = simpleLightFactor(pos);
        float extinction = d * stepSize;
        float scattering = extinction * lightT;

        vec3 lightColor = vec3(1.0, 0.96, 0.9) * (0.6 + lightT);
        vec3 ambientColor = vec3(0.5, 0.6, 0.8) * 0.25;
        vec3 c = mix(ambientColor, lightColor, lightT);

        color += c * scattering * transmittance;
        transmittance *= exp(-extinction);
        accumulated += scattering;
    }

    fragColor = vec4(color, clamp(accumulated, 0.0, 1.0));
}
