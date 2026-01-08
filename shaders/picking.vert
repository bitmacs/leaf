#version 460 core
// #extension GL_EXT_debug_printf : enable

#include "types.glsl"

layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;

layout (set = 0, binding = 0, std140) uniform CameraBuffer {
    Camera cameras[2];
};

layout (push_constant) uniform PushConstants {
    mat4 model;
    vec3 color;
    uint camera_index;
    uint entity_id;
} push_constants;

void main() {
    // debugPrintfEXT("vertex index: %d, entity id: %d", gl_VertexIndex, push_constants.entity_id);
    gl_Position =  cameras[push_constants.camera_index].projection * cameras[push_constants.camera_index].view * push_constants.model * vec4(position, 1.0);
}
