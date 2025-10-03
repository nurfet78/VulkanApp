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
const int ATMOSPHERE_SAMPLES = 32;
const int LIGHT_SAMPLES = 8;

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
    vec3 rayStart = vec3(0.0, atmosphere.planetRadius + 1000.0, 0.0); // 1km above surface
    
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
    
    // Add sun disk
    float sunAngularRadius = 0.00465; // Sun's angular radius
    float distToSun = acos(clamp(dot(rayDir, atmosphere.sunDirection), -1.0, 1.0));
    if (distToSun < sunAngularRadius) {
        float sunDisk = 1.0 - smoothstep(sunAngularRadius * 0.8, sunAngularRadius, distToSun);
        scattering += vec3(1.0, 0.95, 0.8) * sunDisk * atmosphere.sunIntensity * 100.0;
    }
    
    // Tone mapping
    scattering = 1.0 - exp(-scattering * atmosphere.exposure);
    
    fragColor = vec4(scattering, 1.0);
}