#pragma once

#include "tasks.h"
#include "vk.h"
#include <vector>

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    VkPrimitiveTopology primitive_topology;
};

MeshData generate_triangle_mesh_data();
MeshData generate_plane_mesh_data(float size, uint32_t segments);

#define MAX_MESH_BUFFERS 1024

struct MeshBuffers {
    VkBuffer vertex_buffer;
    VkBuffer index_buffer;
    VkDeviceMemory vertex_buffer_memory;
    VkDeviceMemory index_buffer_memory;

    uint32_t vertex_count;
    uint32_t index_count;
    VkIndexType index_type;
    VkPrimitiveTopology primitive_topology;
};

struct MeshBuffersEntry {
    MeshBuffers mesh_buffers;
    uint32_t ref_count;
    bool uploaded;
};

struct MeshBuffersRegistry {
    MeshBuffersEntry entries[MAX_MESH_BUFFERS];
    std::mutex entries_mutex;
};

uint32_t request_mesh_buffers(MeshBuffersRegistry *mesh_buffers_registry, TaskSystem *task_system, VkContext *context, MeshData &&mesh_data);

void increment_ref_mesh_buffers(MeshBuffersRegistry *mesh_buffers_registry, uint32_t mesh_buffers_handle);
void decrement_ref_mesh_buffers(MeshBuffersRegistry *mesh_buffers_registry, TaskSystem *task_system, VkContext *context, uint32_t mesh_buffers_handle);

bool is_mesh_buffers_uploaded(MeshBuffersRegistry *mesh_buffers_registry, uint32_t mesh_buffers_handle);
