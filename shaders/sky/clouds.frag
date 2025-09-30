// clouds.frag - Volumetric cloud fragment shader
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
layout(binding = 3) uniform sampler2D skyBuffer;

layout(push_constant) uniform CloudPushConstants {
    mat4 invViewProj;
    vec3 cameraPos;
    float time;
    vec3 sunDirection;
    float coverage;
    vec3 windDirection;
    float cloudScale;
} push;

const int CLOUD_STEPS = 64;
const int LIGHT_STEPS = 8;
const float CLOUD_TOP = 4000.0;
const float CLOUD_BOTTOM = 2000.0;

// Noise functions
float fbm(vec3 pos, int octaves) {
    float value = 0.0;
    float amplitude = 1.0;
    float frequency = 1.0;
    
    for (int i = 0; i < octaves; ++i) {
        value += amplitude * (texture(cloudNoise, pos * frequency).r * 2.0 - 1.0);
        amplitude *= clouds.gain;
        frequency *= clouds.lacunarity;
    }
    
    return value * 0.5 + 0.5;
}

// Cloud density function
float cloudDensity(vec3 pos) {
    vec3 animatedPos = pos + push.windDirection * push.time;
    
    // Base noise
    float noise = fbm(animatedPos * clouds.scale * 0.001, clouds.octaves);
    
    // Apply coverage
    noise = smoothstep(1.0 - push.coverage, 1.0, noise);
    
    // Height falloff
    float height = pos.y;
    float heightGradient = smoothstep(CLOUD_BOTTOM, CLOUD_BOTTOM + 500.0, height) * 
                          (1.0 - smoothstep(CLOUD_TOP - 500.0, CLOUD_TOP, height));
    
    // Detail noise
    float detail = texture(cloudDetail, animatedPos * clouds.scale * 0.01).r;
    noise = mix(noise, noise * detail, 0.5);
    
    return clamp(noise * heightGradient * clouds.density, 0.0, 1.0);
}

// Ray-plane intersection for cloud layer
vec2 rayCloudIntersect(vec3 rayStart, vec3 rayDir) {
    float t1 = (CLOUD_BOTTOM - rayStart.y) / rayDir.y;
    float t2 = (CLOUD_TOP - rayStart.y) / rayDir.y;
    
    if (t1 > t2) {
        float temp = t1;
        t1 = t2;
        t2 = temp;
    }
    
    if (t2 < 0.0) return vec2(-1.0);
    
    return vec2(max(t1, 0.0), t2);
}

// Light marching for cloud self-shadowing
float cloudLightMarch(vec3 pos, vec3 lightDir) {
    float lightEnergy = 1.0;
    float stepSize = (CLOUD_TOP - CLOUD_BOTTOM) / float(LIGHT_STEPS);
    
    for (int i = 0; i < LIGHT_STEPS; ++i) {
        pos += lightDir * stepSize;
        if (pos.y > CLOUD_TOP) break;
        
        float density = cloudDensity(pos);
        lightEnergy *= exp(-density * stepSize * 0.1);
        
        if (lightEnergy < 0.01) break;
    }
    
    return lightEnergy;
}

void main() {
    // Reconstruct world space ray
    vec4 target = push.invViewProj * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec3 rayDir = normalize(target.xyz / target.w - push.cameraPos);
    vec3 rayStart = push.cameraPos;
    
    // Intersect with cloud layer
    vec2 cloudIntersect = rayCloudIntersect(rayStart, rayDir);
    
    if (cloudIntersect.x < 0.0) {
        fragColor = vec4(0.0);
        return;
    }
    
    float rayLength = cloudIntersect.y - cloudIntersect.x;
    float stepSize = rayLength / float(CLOUD_STEPS);
    
    vec4 cloudColor = vec4(0.0);
    float transmittance = 1.0;
    
    // March through cloud volume
    for (int i = 0; i < CLOUD_STEPS; ++i) {
        if (transmittance < 0.01) break;
        
        float t = cloudIntersect.x + (float(i) + 0.5) * stepSize;
        vec3 pos = rayStart + rayDir * t;
        
        float density = cloudDensity(pos);
        
        if (density > 0.001) {
            // Lighting calculation
            float lightTransmittance = cloudLightMarch(pos, push.sunDirection);
            
            // Beer's law for extinction
            float extinction = density * stepSize;
            float scattering = extinction * lightTransmittance;
            
            // Color based on light and height
            vec3 lightColor = vec3(1.0, 0.95, 0.8) * (1.0 + lightTransmittance);
            vec3 ambientColor = vec3(0.5, 0.6, 0.8) * 0.3;
            
            vec3 color = mix(ambientColor, lightColor, lightTransmittance);
            
            // Accumulate color
            cloudColor.rgb += color * scattering * transmittance;
            cloudColor.a += scattering * transmittance;
            
            // Update transmittance
            transmittance *= exp(-extinction);
        }
    }
    
    fragColor = cloudColor;
}