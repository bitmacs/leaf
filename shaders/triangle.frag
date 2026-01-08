#version 440 core

layout (location = 0) out vec4 fragColor;

layout (location = 0) in VS_OUT {
    vec3 color;
    vec3 normal; // in world space
} fs_in;

layout (set = 0, binding = 3, std140) uniform DirectionalLight {
    vec3 direction;
} light;

void main() {
    // 简单的 Lambert 光照模型
    vec3 normal = normalize(fs_in.normal);  // 使用从 vertex shader 传入的法线
    vec3 lightDir = normalize(-light.direction);  // 平行光方向（从光源指向表面）

    // 计算漫反射
    float NdotL = max(dot(normal, lightDir), 0.0);
    vec3 directional_light_color = vec3(1.0, 1.0, 1.0);
    vec3 diffuse = fs_in.color * directional_light_color * NdotL;

    // 添加环境光（避免完全黑暗）
    vec3 ambient = fs_in.color * 0.2;

    fragColor = vec4(diffuse + ambient, 1.0);
}
