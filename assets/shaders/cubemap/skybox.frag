// skybox.frag - Fragment shader для skybox
#version 450 core

layout(location = 0) in vec3 texCoord;
layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform samplerCube skyboxTexture;

void main() {
    vec3 color = texture(skyboxTexture, texCoord).rgb;
    fragColor = vec4(color, 1.0);
}