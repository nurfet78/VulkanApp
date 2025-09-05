// shaders/grass.frag
#version 460

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec3 cameraPos;
    float time;
} pc;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 L = normalize(vec3(-0.5, -1.0, -0.5));
    
    // Simple grass shading
    float NdotL = max(dot(N, -L), 0.0);
    float ambient = 0.4;
    float diffuse = NdotL * 0.6;
    
    // Gradient from base to tip
    vec3 baseColor = vec3(0.1, 0.3, 0.05) * fragColor.rgb;
    vec3 tipColor = vec3(0.3, 0.6, 0.1) * fragColor.rgb;
    vec3 grassColor = mix(baseColor, tipColor, fragTexCoord.y);
    
    vec3 finalColor = grassColor * (ambient + diffuse);
    
    // Fade at distance
    float dist = distance(pc.cameraPos, fragWorldPos);
    float fade = smoothstep(50.0, 60.0, dist);
    
    outColor = vec4(finalColor, 1.0 - fade);
}