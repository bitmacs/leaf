#include "meshes.h"
#include <cassert>
#include <mutex>

MeshData generate_triangle_mesh_data() {
    MeshData mesh_data;
    mesh_data.vertices = {
        {{-0.5f, -0.5f, 0.0f}},
        {{ 0.5f, -0.5f, 0.0f}},
        {{ 0.0f,  0.5f, 0.0f}},
    };
    mesh_data.indices = {0, 1, 2};
    mesh_data.primitive_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    return mesh_data;
}

uint32_t request_mesh_buffers(MeshBuffersRegistry *mesh_buffers_registry, TaskSystem *task_system, VkContext *context, MeshData &&mesh_data) {
    std::lock_guard<std::mutex> lock(mesh_buffers_registry->entries_mutex);
    for (uint32_t i = 0; i < MAX_MESH_BUFFERS; ++i) {
        if (mesh_buffers_registry->entries[i].ref_count == 0) { // found an unused entry
            mesh_buffers_registry->entries[i].ref_count = 1;
            mesh_buffers_registry->entries[i].uploaded = false;
            std::function<MeshBuffers()> task_body = [context, mesh_data = std::move(mesh_data)]() mutable -> MeshBuffers {
                MeshBuffers mesh_buffers = {};

                {
                    create_buffer(context, sizeof(Vertex) * mesh_data.vertices.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &mesh_buffers.vertex_buffer, &mesh_buffers.vertex_buffer_memory);

                    void *buffer_data = nullptr;
                    vkMapMemory(context->device, mesh_buffers.vertex_buffer_memory, 0, sizeof(Vertex) * mesh_data.vertices.size(), 0, &buffer_data);
                    memcpy(buffer_data, mesh_data.vertices.data(), sizeof(Vertex) * mesh_data.vertices.size());
                    vkUnmapMemory(context->device, mesh_buffers.vertex_buffer_memory);

                    mesh_buffers.vertex_count = mesh_data.vertices.size(); // 保存绘制元数据
                }

                if (!mesh_data.indices.empty()) {
                    create_buffer(context, sizeof(uint32_t) * mesh_data.indices.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &mesh_buffers.index_buffer, &mesh_buffers.index_buffer_memory);

                    void *buffer_data = nullptr;
                    vkMapMemory(context->device, mesh_buffers.index_buffer_memory, 0, sizeof(uint32_t) * mesh_data.indices.size(), 0, &buffer_data);
                    memcpy(buffer_data, mesh_data.indices.data(), sizeof(uint32_t) * mesh_data.indices.size());
                    vkUnmapMemory(context->device, mesh_buffers.index_buffer_memory);

                    mesh_buffers.index_count = mesh_data.indices.size();
                    mesh_buffers.index_type = VK_INDEX_TYPE_UINT32; // 默认使用 uint32 索引
                }
                mesh_buffers.primitive_topology = mesh_data.primitive_topology;
                return mesh_buffers;
            };
            std::function<void(const MeshBuffers &)> task_callback = [mesh_buffers_registry, i](const MeshBuffers &mesh_buffers) mutable {
                std::lock_guard<std::mutex> lock(mesh_buffers_registry->entries_mutex);
                mesh_buffers_registry->entries[i].mesh_buffers = mesh_buffers;
                mesh_buffers_registry->entries[i].uploaded = true;
            };
            std::function<void()> task = [task_body = std::move(task_body), task_callback = std::move(task_callback)]() {
                MeshBuffers mesh_buffers = task_body();
                task_callback(mesh_buffers);
            };
            push_task(task_system, std::move(task));
            return i;
        }
    }
    assert(false);
}

void increment_ref_mesh_buffers(MeshBuffersRegistry *mesh_buffers_registry, uint32_t mesh_buffers_handle) {
    std::lock_guard<std::mutex> lock(mesh_buffers_registry->entries_mutex);
    mesh_buffers_registry->entries[mesh_buffers_handle].ref_count++;
}

void decrement_ref_mesh_buffers(MeshBuffersRegistry *mesh_buffers_registry, TaskSystem *task_system, VkContext *context, uint32_t mesh_buffers_handle) {
    std::lock_guard<std::mutex> lock(mesh_buffers_registry->entries_mutex);
    MeshBuffersEntry &entry = mesh_buffers_registry->entries[mesh_buffers_handle];
    if ((--entry.ref_count) == 0) {
        MeshBuffers mesh_buffers = entry.mesh_buffers;
        memset(&entry, 0, sizeof(MeshBuffersEntry)); // zero out the entry
        push_task(task_system, [context, mesh_buffers]() {
            if (mesh_buffers.index_count > 0) {
                vkDestroyBuffer(context->device, mesh_buffers.index_buffer, nullptr);
                vkFreeMemory(context->device, mesh_buffers.index_buffer_memory, nullptr);
            }
            vkDestroyBuffer(context->device, mesh_buffers.vertex_buffer, nullptr);
            vkFreeMemory(context->device, mesh_buffers.vertex_buffer_memory, nullptr);
        });
    }
}

bool is_mesh_buffers_uploaded(MeshBuffersRegistry *mesh_buffers_registry, uint32_t mesh_buffers_handle) {
    std::lock_guard<std::mutex> lock(mesh_buffers_registry->entries_mutex);
    return mesh_buffers_registry->entries[mesh_buffers_handle].uploaded;
}
