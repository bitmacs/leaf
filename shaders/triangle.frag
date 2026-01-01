#version 440 core

layout (location = 0) out vec4 fragColor;

layout (location = 0) in VS_OUT {
    vec3 color;
} fs_in;

void main() {
    fragColor = vec4(fs_in.color, 1.0);
}
