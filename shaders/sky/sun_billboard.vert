#version 450

layout(location = 0) out vec2 outUV;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec3 sunDirection;
    float sunSize;
    vec3 cameraPos;
    float intensity;
} push;

void main() {
    // Fullscreen quad
    vec2 positions[6] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 1.0, -1.0),
        vec2( 1.0,  1.0),
        vec2(-1.0, -1.0),
        vec2( 1.0,  1.0),
        vec2(-1.0,  1.0)
    );
    
    vec2 pos = positions[gl_VertexIndex];
    outUV = pos * 0.5 + 0.5;
    
    // Calculate sun position in screen space
    vec3 sunWorldPos = push.cameraPos + push.sunDirection * 1000.0;
    vec4 sunClipPos = push.viewProj * vec4(sunWorldPos, 1.0);
    vec2 sunScreenPos = sunClipPos.xy / sunClipPos.w;
    
    // Billboard quad around sun
    vec2 billboardPos = sunScreenPos + pos * push.sunSize;
    
    gl_Position = vec4(billboardPos, 0.99999, 1.0); // Near far plane
}