#version 450

layout(location = 0) out vec2 outUV;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec3 sunDirection;
    float sunSize;
    vec3 cameraPos;
    float intensity;
    float aspectRatio;
} push;

void main() {
    // Fullscreen triangle vertices
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
    
    // Calculate sun position
    vec3 sunWorldPos = push.cameraPos + push.sunDirection * 10000.0;
    vec4 sunClipPos = push.viewProj * vec4(sunWorldPos, 1.0);
    
    // Cull if behind camera
    if (sunClipPos.w <= 0.0) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0); // Outside clip space
        return;
    }
    
    vec2 sunNDC = sunClipPos.xy / sunClipPos.w;
    
    // Apply aspect correction
    vec2 correctedSize = push.sunSize * vec2(1.0 / push.aspectRatio, 1.0);
    vec2 billboardPos = sunNDC + pos * correctedSize;
    
    gl_Position = vec4(billboardPos, 0.99999, 1.0);
}