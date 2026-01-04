#version 440 core
#extension GL_EXT_debug_printf : enable

layout (set = 0, binding = 1, std430) writeonly buffer PickingResult {
    uint entity_id;
} picking_result;

layout (push_constant) uniform PushConstants {
    mat4 model;
    vec3 color;
    uint camera_index;
    uint entity_id;
} push_constants;

void main() {
    // debugPrintfEXT("frag coord: (%f, %f), entity id: %d", gl_FragCoord.x, gl_FragCoord.y, push_constants.entity_id);
    atomicExchange(picking_result.entity_id, push_constants.entity_id);
}
