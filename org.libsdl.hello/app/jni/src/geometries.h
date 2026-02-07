#pragma once

#include "tasks.h"
#include "vk.h"
#include <string>
#include <vector>

struct GeometryData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    VkPrimitiveTopology primitive_topology;
};

GeometryData generate_triangle_geometry_data();
GeometryData generate_plane_geometry_data(float width, float height, uint32_t segments);
GeometryData generate_cube_geometry_data(float size);
GeometryData generate_sphere_geometry_data(float radius, uint32_t segments);
GeometryData generate_cylinder_geometry_data(float radius, float height, uint32_t radial_segments);

std::vector<GeometryData> load_gltf_geometry_data(const std::string &filepath);

#define MAX_GEOMETRIES 1024

struct Geometry {
    VkBuffer vertex_buffer;
    VkBuffer index_buffer;
    VkDeviceMemory vertex_buffer_memory;
    VkDeviceMemory index_buffer_memory;

    uint32_t vertex_count;
    uint32_t index_count;
    VkIndexType index_type;
    VkPrimitiveTopology primitive_topology;

    VkAccelerationStructureKHR blas;
    VkBuffer blas_buffer;  // 存储构建好的 blas 数据（硬件 bvh 树）
    VkDeviceMemory blas_memory;
};

struct GeometryEntry {
    Geometry geometry;
    uint32_t ref_count;
    bool uploaded;
};

struct GeometryRegistry {
    GeometryEntry entries[MAX_GEOMETRIES];
    std::mutex entries_mutex;
};

uint32_t request_geometry(GeometryRegistry *geometry, TaskSystem *task_system, VkContext *context, GeometryData &&geometry_data);

void increment_geometry_ref(GeometryRegistry *geometry_registry, uint32_t geometry_handle);
void decrement_geometry_ref(GeometryRegistry *geometry_registry, TaskSystem *task_system, VkContext *context, uint32_t geometry_handle);

bool is_geometry_uploaded(GeometryRegistry *geometry_registry, uint32_t geometry_handle);
