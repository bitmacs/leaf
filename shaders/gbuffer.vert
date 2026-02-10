#version 460 core

#include "types.glsl"

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

layout(set = 0, binding = 0, std140) uniform CameraBuffer {
    Camera cameras[2];
};

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec3 color;
    uint camera_index;
    uint entity_id;
} push_constants;

layout(location = 0) out vec3 out_position_world;
layout(location = 1) out vec3 out_normal_world;
layout(location = 2) out vec3 out_albedo;
layout(location = 3) out vec3 out_position_view;

void main() {
    vec4 world_pos = push_constants.model * vec4(position, 1.0);
    vec4 view_pos = cameras[push_constants.camera_index].view * world_pos;
    gl_Position = cameras[push_constants.camera_index].projection * view_pos;
    out_position_world = world_pos.xyz;
    out_albedo = push_constants.color;
    mat3 normal_matrix = mat3(transpose(inverse(push_constants.model)));
    out_normal_world = normalize(normal_matrix * normal);
    out_position_view = view_pos.xyz;
}
