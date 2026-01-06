#version 440 core
#extension GL_EXT_debug_printf : enable

// 启用 early fragment tests，确保深度测试在 fragment shader 执行之前进行
// 这样只有通过深度测试的片段才会执行 shader
// 注意：需要配合 VK_COMPARE_OP_LESS_OR_EQUAL 使用，否则深度值等于深度缓冲区值的片段（最前面的物体）无法通过深度测试
layout (early_fragment_tests) in;

layout (set = 0, binding = 1, std430) buffer PickingResult {
    uint entity_id;
    uint min_depth_bits;  // 用于跟踪最小深度值（以整数形式存储，便于 atomicMin）
} picking_result;

layout (push_constant) uniform PushConstants {
    mat4 model;
    vec3 color;
    uint camera_index;
    uint entity_id;
} push_constants;

void main() {
    // debugPrintfEXT("fragment coord: %f, %f, %f, entity id: %d", gl_FragCoord.x, gl_FragCoord.y, gl_FragCoord.z, push_constants.entity_id);
    // 获取当前片段的深度值，转换为整数（使用 24 位精度）
    // gl_FragCoord.z 的范围是 [0, 1]，深度值越小表示越近（越前面）
    // 我们将其转换为 [0, 0xFFFFFF] 的整数，深度值越小，整数也越小
    uint current_depth_bits = uint(gl_FragCoord.z * 16777215.0);  // 2^24 - 1 = 16777215

    // 使用 atomicMin 来跟踪最小深度值（即最前面的物体）
    // atomicMin 会原子地比较并更新最小值，返回之前的值
    uint prev_min_depth_bits = atomicMin(picking_result.min_depth_bits, current_depth_bits);

    // 如果当前深度值 <= 之前的最小深度值，说明我们更新了最小值，应该更新 entity_id
    // 注意：由于多个片段可能同时执行，我们需要确保只有真正更新了最小值的片段才更新 entity_id
    if (current_depth_bits <= prev_min_depth_bits) {
        atomicExchange(picking_result.entity_id, push_constants.entity_id);
    }
}
