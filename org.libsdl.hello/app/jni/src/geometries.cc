#include "geometries.h"
#include <cassert>
#include <mutex>

GeometryData generate_triangle_geometry_data() {
    GeometryData mesh_data;
    // 等边三角形，中心在原点 (0, 0, 0)
    // 三个顶点均匀分布在圆周上，角度间隔为 120 度
    // 顶点1：顶部 (90°) = (0, r, 0)
    // 顶点2：左下 (210°) = (-r*√3/2, -r/2, 0)
    // 顶点3：右下 (330°) = (r*√3/2, -r/2, 0)
    const float r = 0.5f;  // 中心到顶点的距离
    const float sqrt3_half = 0.86602540378f;  // √3/2
    mesh_data.vertices = {
        {{ 0.0f,        r, 0.0f}},  // 顶部
        {{-r * sqrt3_half, -r * 0.5f, 0.0f}},  // 左下
        {{ r * sqrt3_half, -r * 0.5f, 0.0f}},  // 右下
    };
    mesh_data.indices = {0, 1, 2};
    mesh_data.primitive_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    return mesh_data;
}

GeometryData generate_plane_geometry_data(float size, uint32_t segments) {
    GeometryData mesh;

    float step = size / (float) segments;
    float half_size = size * 0.5f;

    // 生成顶点
    for (uint32_t z = 0; z <= segments; ++z) {
        for (uint32_t x = 0; x <= segments; ++x) {
            float x_pos = -half_size + x * step;
            float z_pos = -half_size + z * step;

            mesh.vertices.push_back({
                {x_pos, 0.0f, z_pos}
            });
        }
    }

    // 生成索引
    for (uint32_t z = 0; z < segments; ++z) {
        for (uint32_t x = 0; x < segments; ++x) {
            uint32_t top_left = z * (segments + 1) + x;
            uint32_t top_right = top_left + 1;
            uint32_t bottom_left = (z + 1) * (segments + 1) + x;
            uint32_t bottom_right = bottom_left + 1;

            // 第一个三角形
            mesh.indices.push_back(top_left);
            mesh.indices.push_back(bottom_left);
            mesh.indices.push_back(top_right);

            // 第二个三角形
            mesh.indices.push_back(top_right);
            mesh.indices.push_back(bottom_left);
            mesh.indices.push_back(bottom_right);
        }
    }

    mesh.primitive_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    return mesh;
}

uint32_t request_geometry(GeometryRegistry *geometry_registry, TaskSystem *task_system, VkContext *context, GeometryData &&geometry_data) {
    std::lock_guard<std::mutex> lock(geometry_registry->entries_mutex);
    for (uint32_t i = 0; i < MAX_GEOMETRIES; ++i) {
        if (geometry_registry->entries[i].ref_count == 0) { // found an unused entry
            geometry_registry->entries[i].ref_count = 1;
            geometry_registry->entries[i].uploaded = false;
            std::function<Geometry()> task_body = [context, mesh_data = std::move(geometry_data)]() mutable -> Geometry {
                Geometry geometry = {};

                {
                    create_buffer(context, sizeof(Vertex) * mesh_data.vertices.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &geometry.vertex_buffer, &geometry.vertex_buffer_memory);

                    void *buffer_data = nullptr;
                    vkMapMemory(context->device, geometry.vertex_buffer_memory, 0, sizeof(Vertex) * mesh_data.vertices.size(), 0, &buffer_data);
                    memcpy(buffer_data, mesh_data.vertices.data(), sizeof(Vertex) * mesh_data.vertices.size());
                    vkUnmapMemory(context->device, geometry.vertex_buffer_memory);

                    geometry.vertex_count = mesh_data.vertices.size(); // 保存绘制元数据
                }

                if (!mesh_data.indices.empty()) {
                    create_buffer(context, sizeof(uint32_t) * mesh_data.indices.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &geometry.index_buffer, &geometry.index_buffer_memory);

                    void *buffer_data = nullptr;
                    vkMapMemory(context->device, geometry.index_buffer_memory, 0, sizeof(uint32_t) * mesh_data.indices.size(), 0, &buffer_data);
                    memcpy(buffer_data, mesh_data.indices.data(), sizeof(uint32_t) * mesh_data.indices.size());
                    vkUnmapMemory(context->device, geometry.index_buffer_memory);

                    geometry.index_count = mesh_data.indices.size();
                    geometry.index_type = VK_INDEX_TYPE_UINT32; // 默认使用 uint32 索引
                }
                geometry.primitive_topology = mesh_data.primitive_topology;
                return geometry;
            };
            std::function<void(const Geometry &)> task_callback = [geometry_registry, i](const Geometry &geometry) mutable {
                std::lock_guard<std::mutex> lock(geometry_registry->entries_mutex);
                geometry_registry->entries[i].geometry = geometry;
                geometry_registry->entries[i].uploaded = true;
            };
            std::function<void()> task = [task_body = std::move(task_body), task_callback = std::move(task_callback)]() {
                Geometry geometry = task_body();
                task_callback(geometry);
            };
            push_task(task_system, std::move(task));
            return i;
        }
    }
    assert(false);
}

void increment_ref_geometry(GeometryRegistry *geometry_registry, uint32_t geometry_handle) {
    std::lock_guard<std::mutex> lock(geometry_registry->entries_mutex);
    geometry_registry->entries[geometry_handle].ref_count++;
}

void decrement_ref_geometry(GeometryRegistry *geometry_registry, TaskSystem *task_system, VkContext *context, uint32_t geometry_handle) {
    std::lock_guard<std::mutex> lock(geometry_registry->entries_mutex);
    GeometryEntry &entry = geometry_registry->entries[geometry_handle];
    if ((--entry.ref_count) == 0) {
        Geometry geometry = entry.geometry;
        memset(&entry, 0, sizeof(GeometryEntry)); // zero out the entry
        push_task(task_system, [context, geometry]() {
            if (geometry.index_count > 0) {
                vkDestroyBuffer(context->device, geometry.index_buffer, nullptr);
                vkFreeMemory(context->device, geometry.index_buffer_memory, nullptr);
            }
            vkDestroyBuffer(context->device, geometry.vertex_buffer, nullptr);
            vkFreeMemory(context->device, geometry.vertex_buffer_memory, nullptr);
        });
    }
}

bool is_geometry_uploaded(GeometryRegistry *geometry_registry, uint32_t geometry_handle) {
    std::lock_guard<std::mutex> lock(geometry_registry->entries_mutex);
    return geometry_registry->entries[geometry_handle].uploaded;
}
