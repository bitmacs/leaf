#version 460 core
#extension GL_EXT_debug_printf : enable

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

layout (location = 0) out VS_OUT {
    vec3 color;
    vec3 normal; // in world space
    vec3 position; // in world space
} vs_out;

void main() {
    // debugPrintfEXT("vertex index: %d", gl_VertexIndex);
    vec4 world_pos = push_constants.model * vec4(position, 1.0);
    gl_Position =  cameras[push_constants.camera_index].projection * cameras[push_constants.camera_index].view * world_pos;
    vs_out.color = push_constants.color;
    vs_out.position = world_pos.xyz;

    // 将法线从模型空间转换到世界空间（只考虑旋转和缩放，不考虑平移）
    mat3 normal_matrix = mat3(transpose(inverse(push_constants.model)));
    vs_out.normal = normalize(normal_matrix * normal);
}
