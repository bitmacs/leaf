#version 440 core
#extension GL_EXT_debug_printf : enable

layout (location = 0) in vec3 position;

layout (set = 0, binding = 0, std140) uniform CameraBuffer {
    mat4 view;
    mat4 projection;
} cameras[2];

layout (push_constant) uniform PushConstants {
    mat4 model;
    vec3 color;
    uint camera_index;
} push_constants;

layout (location = 0) out VS_OUT {
    vec3 color;
} vs_out;

void main() {
    // debugPrintfEXT("vertex index: %d", gl_VertexIndex);
    gl_Position =  cameras[push_constants.camera_index].projection * cameras[push_constants.camera_index].view * push_constants.model * vec4(position, 1.0);
    vs_out.color = push_constants.color;
}
