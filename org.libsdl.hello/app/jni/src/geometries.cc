#include "geometries.h"
#include <cassert>
#include <mutex>

GeometryData generate_triangle_geometry_data() {
    GeometryData geometry_data;
    // 等边三角形，中心在原点 (0, 0, 0)
    // 三个顶点均匀分布在圆周上，角度间隔为 120 度
    // 顶点1：顶部 (90°) = (0, r, 0)
    // 顶点2：左下 (210°) = (-r*√3/2, -r/2, 0)
    // 顶点3：右下 (330°) = (r*√3/2, -r/2, 0)
    const float r = 0.5f;  // 中心到顶点的距离
    const float sqrt3_half = 0.86602540378f;  // √3/2
    geometry_data.vertices = {
        {{ 0.0f,        r, 0.0f}},  // 顶部
        {{-r * sqrt3_half, -r * 0.5f, 0.0f}},  // 左下
        {{ r * sqrt3_half, -r * 0.5f, 0.0f}},  // 右下
    };
    geometry_data.indices = {0, 1, 2};
    geometry_data.primitive_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    return geometry_data;
}

GeometryData generate_plane_geometry_data(float size, uint32_t segments) {
    GeometryData geometry_data;

    float step = size / (float) segments;
    float half_size = size * 0.5f;

    // 生成顶点
    for (uint32_t z = 0; z <= segments; ++z) {
        for (uint32_t x = 0; x <= segments; ++x) {
            float x_pos = -half_size + x * step;
            float z_pos = -half_size + z * step;

            geometry_data.vertices.push_back({
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
            geometry_data.indices.push_back(top_left);
            geometry_data.indices.push_back(bottom_left);
            geometry_data.indices.push_back(top_right);

            // 第二个三角形
            geometry_data.indices.push_back(top_right);
            geometry_data.indices.push_back(bottom_left);
            geometry_data.indices.push_back(bottom_right);
        }
    }

    geometry_data.primitive_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    return geometry_data;
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
                    uint64_t buffer_size = sizeof(Vertex) * mesh_data.vertices.size();
                    VkBufferUsageFlags buffer_usage_flags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
                    VkMemoryPropertyFlags memory_property_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                    VkBuffer staging_buffer = {};
                    VkDeviceMemory staging_buffer_memory = {};
                    create_buffer(context, buffer_size, buffer_usage_flags, memory_property_flags, &geometry.vertex_buffer, &geometry.vertex_buffer_memory);

                    buffer_usage_flags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
                    memory_property_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
                    create_buffer(context, buffer_size, buffer_usage_flags, memory_property_flags, &staging_buffer, &staging_buffer_memory);

                    void *staging_data = nullptr;
                    vkMapMemory(context->device, staging_buffer_memory, 0, buffer_size, 0, &staging_data);
                    memcpy(staging_data, mesh_data.vertices.data(), sizeof(Vertex) * mesh_data.vertices.size());
                    vkUnmapMemory(context->device, staging_buffer_memory);

                    execute_one_time_submit(context, context->transfer_command_pool, context->transfer_queue, [&](VkCommandBuffer command_buffer) {
                        copy_buffer(command_buffer, staging_buffer, geometry.vertex_buffer, buffer_size);
                    });
                    destroy_buffer(context, staging_buffer, staging_buffer_memory);

                    geometry.vertex_count = mesh_data.vertices.size(); // 保存绘制元数据
                }

                if (!mesh_data.indices.empty()) {
                    uint64_t buffer_size = sizeof(uint32_t) * mesh_data.indices.size();
                    VkBufferUsageFlags buffer_usage_flags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
                    VkMemoryPropertyFlags memory_property_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                    VkBuffer staging_buffer = {};
                    VkDeviceMemory staging_buffer_memory = {};
                    create_buffer(context, buffer_size, buffer_usage_flags, memory_property_flags, &geometry.index_buffer, &geometry.index_buffer_memory);

                    buffer_usage_flags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
                    memory_property_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
                    create_buffer(context, buffer_size, buffer_usage_flags, memory_property_flags, &staging_buffer, &staging_buffer_memory);

                    void *staging_data = nullptr;
                    vkMapMemory(context->device, staging_buffer_memory, 0, buffer_size, 0, &staging_data);
                    memcpy(staging_data, mesh_data.indices.data(), sizeof(uint32_t) * mesh_data.indices.size());
                    vkUnmapMemory(context->device, staging_buffer_memory);

                    execute_one_time_submit(context, context->transfer_command_pool, context->transfer_queue, [&](VkCommandBuffer command_buffer) {
                        copy_buffer(command_buffer, staging_buffer, geometry.index_buffer, buffer_size);
                    });
                    destroy_buffer(context, staging_buffer, staging_buffer_memory);

                    geometry.index_count = mesh_data.indices.size();
                    geometry.index_type = VK_INDEX_TYPE_UINT32; // 默认使用 uint32 索引
                }
                geometry.primitive_topology = mesh_data.primitive_topology;
                {
                    // BLAS 构建流程

                    // 描述三角形几何数据，告诉 Vulkan 如何读取顶点和索引数据
                    VkAccelerationStructureGeometryTrianglesDataKHR as_geometry_triangles_data = {};
                    as_geometry_triangles_data.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
                    as_geometry_triangles_data.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
                    as_geometry_triangles_data.vertexData.deviceAddress = get_buffer_device_address(context, geometry.vertex_buffer);
                    as_geometry_triangles_data.vertexStride = sizeof(Vertex);
                    as_geometry_triangles_data.maxVertex = geometry.vertex_count - 1;
                    as_geometry_triangles_data.indexType = geometry.index_count > 0 ? geometry.index_type : VK_INDEX_TYPE_NONE_KHR;
                    as_geometry_triangles_data.indexData.deviceAddress = geometry.index_count > 0 ? get_buffer_device_address(context, geometry.index_buffer) : 0;
                    as_geometry_triangles_data.transformData.deviceAddress = 0;

                    // 包装几何描述，将三角形数据包装成几何描述，指定几何类型和属性
                    VkAccelerationStructureGeometryKHR as_geometry = {};
                    as_geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
                    as_geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
                    as_geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR; // 不透明几何（光线不会进入内部，可以优化遍历）
                    as_geometry.geometry.triangles = as_geometry_triangles_data;

                    // 配置构建信息
                    VkAccelerationStructureBuildGeometryInfoKHR as_build_geometry_info = {};
                    as_build_geometry_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
                    as_build_geometry_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
                    as_build_geometry_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
                    as_build_geometry_info.geometryCount = 1;
                    as_build_geometry_info.pGeometries = &as_geometry;

                    // 计算图元（三角形）数量：如果有索引则使用索引数，否则使用顶点数
                    uint32_t primitive_count = geometry.index_count > 0 ? geometry.index_count / 3 : geometry.vertex_count / 3;

                    // 查询内存需求，返回三个大小：AS 本身大小、构建临时内存、更新临时内存
                    VkAccelerationStructureBuildSizesInfoKHR as_build_sizes_info = {};
                    as_build_sizes_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
                    context->vkGetAccelerationStructureBuildSizesKHR(context->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &as_build_geometry_info, &primitive_count, &as_build_sizes_info);

                    // 分配 BLAS 存储缓冲区
                    VkMemoryPropertyFlags blas_memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                    VkBufferUsageFlags blas_buffer_usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
                    create_buffer(context, as_build_sizes_info.accelerationStructureSize, blas_buffer_usage, blas_memory_properties, &geometry.blas_buffer, &geometry.blas_memory);

                    // 创建加速结构对象（此时还没有数据，数据需要构建后才有）
                    VkAccelerationStructureCreateInfoKHR as_create_info = {};
                    as_create_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
                    as_create_info.buffer = geometry.blas_buffer; // 关联存储缓冲区
                    as_create_info.size = as_build_sizes_info.accelerationStructureSize; // 缓冲区大小
                    as_create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
                    VkResult result = context->vkCreateAccelerationStructureKHR(context->device, &as_create_info, nullptr, &geometry.blas);
                    assert(result == VK_SUCCESS);

                    // 分配 Scratch 缓冲区（临时工作空间）
                    VkBuffer scratch_buffer;
                    VkDeviceMemory scratch_buffer_memory;
                    VkBufferUsageFlags scratch_buffer_usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
                    create_buffer(context, as_build_sizes_info.buildScratchSize, scratch_buffer_usage, blas_memory_properties, &scratch_buffer, &scratch_buffer_memory);

                    // 更新构建信息，告诉构建命令将结果写入哪里，使用哪个 scratch buffer
                    as_build_geometry_info.dstAccelerationStructure = geometry.blas;
                    as_build_geometry_info.scratchData.deviceAddress = get_buffer_device_address(context, scratch_buffer);

                    // 准备构建范围，指定构建哪些图元（支持部分构建和多几何体）
                    VkAccelerationStructureBuildRangeInfoKHR build_range_info = {};
                    build_range_info.primitiveCount = primitive_count;
                    build_range_info.primitiveOffset = 0;
                    build_range_info.firstVertex = 0;
                    build_range_info.transformOffset = 0;

                    const VkAccelerationStructureBuildRangeInfoKHR *p_build_range_infos = &build_range_info;

                    execute_one_time_submit(context, context->transfer_command_pool, context->transfer_queue, [&](VkCommandBuffer command_buffer) {
                        context->vkCmdBuildAccelerationStructuresKHR(command_buffer, 1, &as_build_geometry_info, &p_build_range_infos);
                    });

                    destroy_buffer(context, scratch_buffer, scratch_buffer_memory);
                }
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

void increment_geometry_ref(GeometryRegistry *geometry_registry, uint32_t geometry_handle) {
    std::lock_guard<std::mutex> lock(geometry_registry->entries_mutex);
    geometry_registry->entries[geometry_handle].ref_count++;
}

void decrement_geometry_ref(GeometryRegistry *geometry_registry, TaskSystem *task_system, VkContext *context, uint32_t geometry_handle) {
    std::lock_guard<std::mutex> lock(geometry_registry->entries_mutex);
    GeometryEntry &entry = geometry_registry->entries[geometry_handle];
    if ((--entry.ref_count) == 0) {
        Geometry geometry = entry.geometry;
        memset(&entry, 0, sizeof(GeometryEntry)); // zero out the entry
        push_task(task_system, [context, geometry]() {
            context->vkDestroyAccelerationStructureKHR(context->device, geometry.blas, nullptr);
            destroy_buffer(context, geometry.blas_buffer, geometry.blas_memory);
            if (geometry.index_count > 0) {
                destroy_buffer(context, geometry.index_buffer, geometry.index_buffer_memory);
            }
            destroy_buffer(context, geometry.vertex_buffer, geometry.vertex_buffer_memory);
        });
    }
}

bool is_geometry_uploaded(GeometryRegistry *geometry_registry, uint32_t geometry_handle) {
    std::lock_guard<std::mutex> lock(geometry_registry->entries_mutex);
    return geometry_registry->entries[geometry_handle].uploaded;
}
