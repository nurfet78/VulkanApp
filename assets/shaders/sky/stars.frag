// stars.frag - Star field fragment shader
#version 450 core

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform StarUniforms {
    float intensity;
    float twinkle;
    float milkyWayIntensity;
    float time;
} stars;

layout(binding = 1) uniform sampler2D starTexture; 
layout(binding = 2) uniform sampler2D milkyWayTexture; 

layout(push_constant) uniform StarsPushConstants {
    mat4 invViewProj;
    float intensity;
    float twinkle;
    float time;
    float nightBlend;
} push;

// Hash function for procedural stars
float hash(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

// Noise for star twinkling
float noise(vec3 x) {
    vec3 i = floor(x);
    vec3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    
    return mix(mix(mix(hash(i + vec3(0, 0, 0)), 
                       hash(i + vec3(1, 0, 0)), f.x),
                   mix(hash(i + vec3(0, 1, 0)), 
                       hash(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(hash(i + vec3(0, 0, 1)), 
                       hash(i + vec3(1, 0, 1)), f.x),
                   mix(hash(i + vec3(0, 1, 1)), 
                       hash(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}

void main() {
    // Reconstruct world space ray direction
    vec4 target = push.invViewProj * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec3 rayDir = normalize(target.xyz / target.w);
    
    vec3 color = vec3(0.0);
    
    // Generate procedural stars
    float starDensity = hash(floor(rayDir * 1000.0));
    if (starDensity > 0.998) {
        float starBrightness = hash(floor(rayDir * 1001.0));
        
        // Star twinkling
        float twinkle = 1.0 + push.twinkle * sin(push.time * 3.0 + hash(floor(rayDir * 1002.0)) * 100.0);
        
        // Star color variation
        float temperature = hash(floor(rayDir * 1003.0));
        vec3 starColor;
        if (temperature < 0.3) {
            starColor = vec3(1.0, 0.7, 0.5); // Red giant
        } else if (temperature < 0.7) {
            starColor = vec3(1.0, 1.0, 0.8); // Sun-like
        } else {
            starColor = vec3(0.7, 0.8, 1.0); // Blue giant
        }
        
        color += starColor * starBrightness * twinkle * push.intensity;
    }
    
    // Add Milky Way
    vec2 milkyWayUV = vec2(
        atan(rayDir.z, rayDir.x) / (2.0 * 3.14159) + 0.5,
        acos(clamp(rayDir.y, -1.0, 1.0)) / 3.14159
    );
    
    float milkyWay = texture(milkyWayTexture, milkyWayUV).r;
    color += vec3(0.6, 0.7, 1.0) * milkyWay * stars.milkyWayIntensity;
    
    // Apply night blending
    color *= push.nightBlend;
    
    fragColor = vec4(color, 1.0);
}