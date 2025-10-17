// clouds_optimized.frag � Volumetric cloud shader (optimized & stable)
// � ����, 2025

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
float animationOffset;
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
// ��������� ���������� ����� ��� ������� ��������
const int CLOUD_STEPS = 64;
const int LIGHT_STEPS = 6;
const float CLOUD_TOP = 2000.0;
const float CLOUD_BOTTOM = 500.0;
// ���������� ������� FBM � ���������� ����������������
float fbm_improved(vec3 pos) {
float value = 0.0;
float amplitude = 0.5;
float frequency = 1.0;
for (int i = 0; i < 4; ++i) {
    value += amplitude * (texture(cloudNoise, pos * frequency).r * 2.0 - 1.0);
    amplitude *= clouds.gain;
    frequency *= clouds.lacunarity;
}
return value * 0.5 + 0.5;
}

float cloudDensityImproved(vec3 pos) {
    // �������� � ���������� ���������
    // �����: ���������� fract ��� ���������������� ���������� ��� ����������
    vec3 animated = pos * 0.0005 + push.windDirection * (push.time * clouds.speed * 0.01);
    
    // ������� ��� � �������������� �������� �� ������ ��������
    // ���������� fract ��� ���������� ��������
    vec3 baseCoord = fract(animated * clouds.scale * 0.001);
    float base = fbm_improved(baseCoord);
    
    // ��������� ��� �� ������ �������
    vec3 detailCoord = fract(animated * clouds.scale * 0.003);
    float detail = texture(cloudDetail, detailCoord).r;
    
    // �������������� ���� ���� ��� ��������� ������������� ���������
    vec3 breakupCoord = fract(animated * clouds.scale * 0.0015 + vec3(0.5));
    float breakup = texture(cloudNoise, breakupCoord).g;
    
    // ��������: ���������� ��������� � ������ coverage
    // ��������� ��������� ���� ���� ��� ���������� ���������
    float combinedNoise = base * 0.7 + breakup * 0.3;
    
    float coverageThreshold = 1.0 - push.coverage;
    
    // �������� ������� ��� � �������� � ������ ������
    float baseRemapped = smoothstep(coverageThreshold - 0.1, coverageThreshold + 0.15, combinedNoise);
    
    // ���� ���� ������ - ��� ������ ������
    if (baseRemapped < 0.001) return 0.0;
    
    // ������ ���� ��������� �����
    float detailErosion = mix(1.0, detail, 0.35);
    float finalCloud = baseRemapped * detailErosion;
    
    // �������������� "������" ��� �������� ������ ����
    finalCloud = smoothstep(0.15, 0.85, finalCloud);
    
    // �������� ��������
    float h = pos.y;
    float heightGradient = smoothstep(CLOUD_BOTTOM, CLOUD_BOTTOM + 300.0, h) *
                          (1.0 - smoothstep(CLOUD_TOP - 400.0, CLOUD_TOP, h));
    
    // �����������
    float d = finalCloud * heightGradient * clouds.density * 0.3;
    
    // ������� ������ ���
    d = max(0.0, d - 0.05);
    
    return clamp(d, 0.0, 1.0);
}
// ���������� ��������� ������� Beer-Lambert + powder effect
float getCloudLighting(vec3 pos, float density) {
// Raymarch � ������
float lightEnergy = 1.0;
float stepSize = 100.0;
for (int i = 0; i < LIGHT_STEPS; i++) {
    vec3 samplePos = pos + push.sunDirection * stepSize * float(i);
    
    // ���������, �� ����� �� �� ������� ���� �������
    if (samplePos.y > CLOUD_TOP || samplePos.y < CLOUD_BOTTOM) break;
    
    float sampleDensity = cloudDensityImproved(samplePos);
    lightEnergy *= exp(-sampleDensity * stepSize * 0.003);
    
    if (lightEnergy < 0.01) break;
}

// Powder effect (Beer's powder approximation)
// ������ ������ ����� "���������" �� ���������� �����
float powderEffect = 1.0 - exp(-density * 2.0);
lightEnergy = mix(lightEnergy, powderEffect, 0.5);

// Ambient occlusion �� ������ ���������
float ao = exp(-density * 0.5);

return mix(ao * 0.3, 1.0, lightEnergy);
}
// Ambient light � ������ ������ � ����������� � ������
vec3 getAmbientLight(vec3 pos) {
float heightFactor = clamp((pos.y - CLOUD_BOTTOM) / (CLOUD_TOP - CLOUD_BOTTOM), 0.0, 1.0);
// ���� ���� ������� �� ��������� ������
float sunHeight = max(0.0, push.sunDirection.y);
vec3 skyColor = mix(
    vec3(0.4, 0.5, 0.7),  // ������ ���� (�����)
    vec3(0.6, 0.7, 0.9),  // ������� ���� (�������)
    sunHeight
);

vec3 groundColor = vec3(0.3, 0.35, 0.4);

return mix(groundColor, skyColor, heightFactor);
}
// Ray intersection � �������� �����
vec2 rayCloudIntersect(vec3 rayStart, vec3 rayDir) {
float t1 = (CLOUD_BOTTOM - rayStart.y) / rayDir.y;
float t2 = (CLOUD_TOP - rayStart.y) / rayDir.y;
if (t1 > t2) {
    float tmp = t1; t1 = t2; t2 = tmp;
}
if (t2 < 0.0) return vec2(-1.0);

return vec2(max(t1, 0.0), t2);
}
void main() {
// ��������������� ����������� ����
vec4 target = push.invViewProj * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
vec3 rayDir = normalize(target.xyz / target.w - push.cameraPos);
vec3 rayStart = push.cameraPos;
// ����������� � �������� �����
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

// Dithering ��� ���������� ��������
float dither = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
float startOffset = dither * stepSize;

// Ray marching
for (int i = 0; i < CLOUD_STEPS; ++i) {
    if (transmittance < 0.01) break;
    
    float t = cloudIntersect.x + startOffset + float(i) * stepSize;
    vec3 pos = rayStart + rayDir * t;
    
    float density = cloudDensityImproved(pos);
    
    if (density > 0.001) {
        // ���������
        float lighting = getCloudLighting(pos, density);
        vec3 ambient = getAmbientLight(pos);
        
        // ���� ������ ������� �� ��� ������
        float sunHeight = max(0.0, push.sunDirection.y);
        vec3 sunColor = mix(
            vec3(1.0, 0.6, 0.3),  // �����
            vec3(1.0, 0.98, 0.95), // ����
            sunHeight
        );
        
        vec3 cloudColor = mix(ambient, sunColor, lighting);
        
        // Scattering � extinction
        float extinction = density * stepSize;
        float scattering = extinction * (1.0 - exp(-extinction));
        
        color += cloudColor * scattering * transmittance;
        transmittance *= exp(-extinction);
        accumulated += scattering;
    }
}

// ��������� �����
float alpha = clamp(accumulated, 0.0, 1.0);

fragColor = vec4(color, alpha);
}