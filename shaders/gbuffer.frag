#version 460 core

layout(location = 0) in vec3 out_position_world;
layout(location = 1) in vec3 out_normal_world;
layout(location = 2) in vec3 out_albedo;
layout(location = 3) in vec3 out_position_view;

// MRT: position, normal, albedo, linear depth (与 path_tracing 读取的 gbuffer 格式一致)
layout(location = 0) out vec4 gbuffer_position;
layout(location = 1) out vec4 gbuffer_normal;
layout(location = 2) out vec4 gbuffer_albedo;
layout(location = 3) out vec4 gbuffer_depth;

void main() {
    gbuffer_position = vec4(out_position_world, 1.0);
    gbuffer_normal = vec4(normalize(out_normal_world), 1.0);
    gbuffer_albedo = vec4(out_albedo, 1.0);
    // 线性深度：相机到片元的距离，path_tracing 用 hit.t 表示同一含义
    float linear_depth = length(out_position_view);
    gbuffer_depth = vec4(linear_depth, 0.0, 0.0, 0.0);
}
