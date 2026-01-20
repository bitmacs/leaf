#version 460 core
#extension GL_EXT_ray_query : require

#include "types.glsl"

layout (location = 0) out vec4 fragColor;

layout (location = 0) in VS_OUT {
    vec3 color;
    vec3 normal; // in world space
    vec3 position; // in world space
} fs_in;

layout (set = 0, binding = 2) uniform accelerationStructureEXT topLevelAS;

layout (set = 0, binding = 3, std140) uniform DirectionalLight {
    vec3 direction;
} light;

// struct RayDesc {
//     vec3 origin;
//     vec3 direction;
//     float t_min;
//     float t_max;
// };

// 计算硬阴影（基于 Ray Query）
float computeHardShadow(vec3 position, vec3 normal, vec3 light_dir) {
    // 避免自相交：从表面沿法线方向稍微偏移
    vec3 ray_origin = position + normal * EPSILON;

    // 平行光方向（从表面指向光源）
    vec3 ray_dir = normalize(light_dir);

    // 使用足够大的距离（平行光可以认为是无限远）
    float ray_length = 1000.0;

    // 初始化光线查询
    rayQueryEXT ray_query;
    rayQueryInitializeEXT(ray_query,
                          topLevelAS,
                          gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT,
                          0xFF,  // 所有实例
                          ray_origin,
                          EPSILON, // t_min: 避免自相交
                          ray_dir,
                          ray_length); // t_max: 光线长度

    // 执行光线查询（需要循环直到完成）
    while(rayQueryProceedEXT(ray_query)) {
        // 使用 TerminateOnFirstHit 标志时，第一次命中后会自动停止
        // 这里不需要额外处理，循环会自然结束
    }

    // 检查是否有交点（检查committed intersection）
    bool is_occluded = (rayQueryGetIntersectionTypeEXT(ray_query, true) != gl_RayQueryCommittedIntersectionNoneEXT);

    // 返回阴影因子：1.0 = 有光照，0.0 = 在阴影中
    return is_occluded ? 0.0 : 1.0;
}

void main() {
    // 简单的 Lambert 光照模型
    vec3 normal = normalize(fs_in.normal);  // 使用从 vertex shader 传入的法线
    vec3 light_dir = normalize(-light.direction);  // 平行光方向（从表面指向光源，用于光照计算）

    // 计算硬阴影（从表面向光源发射光线，检查是否被遮挡）
    float shadow = computeHardShadow(fs_in.position, normal, light_dir);

    // fragColor = vec4(shadow, shadow, shadow, 1.0);
    // return;

    // 计算漫反射
    float NdotL = max(dot(normal, light_dir), 0.0);
    vec3 directional_light_color = vec3(1.0, 1.0, 1.0);
    vec3 diffuse = fs_in.color * directional_light_color * NdotL * shadow;

    // 添加环境光（避免完全黑暗）
    vec3 ambient = fs_in.color * 0.2;

    fragColor = vec4(diffuse + ambient, 1.0);
}
