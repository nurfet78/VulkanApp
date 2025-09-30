// postprocess.frag - HDR tone mapping and bloom
#version 450 core

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform sampler2D skyBuffer;
layout(binding = 1) uniform sampler2D cloudBuffer;
layout(binding = 2) uniform sampler2D bloomBuffer;

layout(push_constant) uniform PostProcessPushConstants {
    float exposure;
    float bloomThreshold;
    float bloomIntensity;
    float time;
} push;

// ACES tone mapping
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 skyColor = texture(skyBuffer, uv).rgb;
    vec4 clouds = texture(cloudBuffer, uv);
    
    // Composite clouds with sky
    vec3 color = mix(skyColor, clouds.rgb, clouds.a);
    
    // Apply exposure
    color *= push.exposure;
    
    // Tone mapping
    color = ACESFilm(color);
    
    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));
    
    fragColor = vec4(color, 1.0);
}