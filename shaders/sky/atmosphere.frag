// atmosphere.frag - Atmospheric scattering fragment shader
#version 450 core

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform AtmosphereUniforms {
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
    mat4 invViewProj;
    vec3 cameraPos;
    float time;
} push;

const float PI = 3.14159265359;
const int ATMOSPHERE_SAMPLES = 16;
const int LIGHT_SAMPLES = 4;

// Rayleigh phase function
float rayleighPhase(float cosTheta) {
    return 3.0 / (16.0 * PI) * (1.0 + cosTheta * cosTheta);
}

// Henyey-Greenstein phase function for Mie scattering
float miePhase(float cosTheta, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * PI * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5));
}

// Compute optical depth through atmosphere
float computeOpticalDepth(vec3 rayStart, vec3 rayDir, float rayLength, float planetRadius, float atmosphereRadius) {
    float stepSize = rayLength / float(LIGHT_SAMPLES);
    float opticalDepth = 0.0;
    
    for (int i = 0; i < LIGHT_SAMPLES; ++i) {
        vec3 samplePoint = rayStart + rayDir * (float(i) + 0.5) * stepSize;
        float height = length(samplePoint) - planetRadius;
        float density = exp(-height / 8000.0); // Scale height 8km
        opticalDepth += density * stepSize;
    }
    
    return opticalDepth;
}

// Ray-sphere intersection
vec2 raySphereIntersect(vec3 rayStart, vec3 rayDir, float sphereRadius) {
    float a = dot(rayDir, rayDir);
    float b = 2.0 * dot(rayStart, rayDir);
    float c = dot(rayStart, rayStart) - sphereRadius * sphereRadius;
    float discriminant = b * b - 4.0 * a * c;
    
    if (discriminant < 0.0) {
        return vec2(-1.0);
    }
    
    float sqrtD = sqrt(discriminant);
    return vec2((-b - sqrtD) / (2.0 * a), (-b + sqrtD) / (2.0 * a));
}

// Compute single scattering
vec3 computeScattering(vec3 rayStart, vec3 rayDir, float rayLength, vec3 sunDir) {
    vec3 rayleighScattering = vec3(0.0);
    vec3 mieScattering = vec3(0.0);
    
    float stepSize = rayLength / float(ATMOSPHERE_SAMPLES);
    float rayleighOpticalDepth = 0.0;
    float mieOpticalDepth = 0.0;
    
    for (int i = 0; i < ATMOSPHERE_SAMPLES; ++i) {
        float t = (float(i) + 0.5) * stepSize;
        vec3 samplePoint = rayStart + rayDir * t;
        float height = length(samplePoint) - atmosphere.planetRadius;
        
        // Density at sample point
        float rayleighDensity = exp(-height / 8000.0);
        float mieDensity = exp(-height / 1200.0);
        
        rayleighOpticalDepth += rayleighDensity * stepSize;
        mieOpticalDepth += mieDensity * stepSize;
        
        // Optical depth from sample point to sun
        vec2 sunIntersect = raySphereIntersect(samplePoint, sunDir, atmosphere.atmosphereRadius);
        float sunRayLength = sunIntersect.y;
        
        if (sunRayLength > 0.0) {
            float sunRayleighOpticalDepth = computeOpticalDepth(samplePoint, sunDir, sunRayLength, 
                atmosphere.planetRadius, atmosphere.atmosphereRadius) * 0.8; // Rayleigh extinction
            float sunMieOpticalDepth = computeOpticalDepth(samplePoint, sunDir, sunRayLength, 
                atmosphere.planetRadius, atmosphere.atmosphereRadius) * 0.2; // Mie extinction
            
            vec3 transmittance = exp(-atmosphere.rayleighBeta * (rayleighOpticalDepth + sunRayleighOpticalDepth) 
                                   - atmosphere.mieBeta * atmosphere.mieCoeff * (mieOpticalDepth + sunMieOpticalDepth));
            
            rayleighScattering += transmittance * rayleighDensity * stepSize;
            mieScattering += transmittance * mieDensity * stepSize;
        }
    }
    
    // Apply phase functions
    float cosTheta = dot(rayDir, sunDir);
    rayleighScattering *= atmosphere.rayleighBeta * rayleighPhase(cosTheta);
    mieScattering *= atmosphere.mieBeta * atmosphere.mieCoeff * miePhase(cosTheta, atmosphere.mieG);
    
    return (rayleighScattering + mieScattering) * atmosphere.sunIntensity;
}

void main() {
   // Reconstruct world space ray direction
 vec4 target = push.invViewProj * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
 vec3 rayDir = normalize(target.xyz / target.w - push.cameraPos);
 
 // Ray origin (camera position relative to planet center)
 vec3 rayStart = vec3(0, atmosphere.planetRadius + 1000.0, 0) + push.cameraPos;
 
 // Intersect with atmosphere
 vec2 atmosphereIntersect = raySphereIntersect(rayStart, rayDir, atmosphere.atmosphereRadius);
 vec2 groundIntersect = raySphereIntersect(rayStart, rayDir, atmosphere.planetRadius);
 
 vec3 scattering = vec3(0.0);
 
 if (atmosphereIntersect.y > 0.0) {
     float rayLength = atmosphereIntersect.y;
     
     // If ray hits ground, limit to ground intersection
     if (groundIntersect.x > 0.0) {
         rayLength = min(rayLength, groundIntersect.x);
     }
     
     scattering = computeScattering(rayStart, rayDir, rayLength, atmosphere.sunDirection);
 }
 
    
    // === солнечный ореол ===
    float sunDot = dot(rayDir, atmosphere.sunDirection);
    sunDot = clamp(sunDot, 0.0, 1.0);

    // Масштаб от интенсивности солнца
    float intensity = clamp(atmosphere.sunIntensity, 0.5, 5.0);
    float glowScale = mix(0.7, 1.5, smoothstep(0.5, 5.0, intensity));
    float haloStrength = mix(0.4, 1.0, smoothstep(0.5, 5.0, intensity));

    // --- Центральное ядро ---
    float core = smoothstep(0.985 - 0.002 * glowScale, 1.0, sunDot);
    scattering += vec3(1.0, 0.97, 0.9) * pow(core, 3.0) * haloStrength;

    // --- Средний ореол ---
    float mid = smoothstep(0.9 - 0.03 * glowScale, 0.985, sunDot);
    scattering += vec3(1.0, 0.94, 0.85) * pow(mid, 2.0) * haloStrength * 0.15;

    // --- Дальний мягкий свет ---
    float soft = smoothstep(0.7 - 0.05 * glowScale, 0.9, sunDot);
    scattering += vec3(0.9, 0.95, 1.0) * pow(soft, 1.2) * haloStrength * 0.05;

    // --- Атмосферное голубое рассеяние ---
    float skyBlend = smoothstep(0.6, 0.85, sunDot);  // чем дальше от солнца — тем сильнее голубой оттенок
    vec3 skyTint = mix(vec3(0.85, 0.95, 1.0), vec3(1.0, 0.97, 0.9), skyBlend);
scattering = mix(scattering, scattering * skyTint, 0.4 * (1.0 - sunDot));
    
    // Tone mapping
    vec3 finalColor = vec3(1.0) - exp(-scattering * atmosphere.exposure);
    
    fragColor = vec4(finalColor, 1.0);
}