#version 440 core
// #extension GL_EXT_debug_printf : enable

layout (location = 0) in vec3 position;

layout (set = 0, binding = 0, std140) uniform CameraBuffer {
    mat4 view;
    mat4 projection;
} cameras[2];

layout (push_constant) uniform PushConstants {
    mat4 model;
    vec3 color;
    uint camera_index;
} instance;

layout (location = 0) out VS_OUT {
    vec3 color;
} vs_out;

void main() {
    // debugPrintfEXT("vertex index: %d", gl_VertexIndex);
    // gl_Position = cameras[instance.camera_index].projection * cameras[instance.camera_index].view * instance.model * vec4(position, 1.0);
    // vs_out.color = instance.color;
    gl_Position = vec4(position, 1.0);
    vs_out.color = vec3(1.0, 0.0, 0.0);
}
