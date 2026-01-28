// Shader 类型定义
// 所有 shader 都可以通过 #include "types.glsl" 使用这些类型定义

const float EPSILON = 0.0001;
const float PI = 3.14159265358979323846;
const float TWO_PI = 6.28318530717958647693;
const float ONE_OVER_PI = 1.0 / PI;
const float ONE_OVER_TWO_PI = 1.0 / (2.0 * PI);
const float ONE_OVER_FOUR_PI = 1.0 / (4.0 * PI);

struct Camera {
    mat4 view;
    mat4 projection;
};

uint pcg_hash(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// generate random float in [0, 1)
float random(uint seed) {
    return float(pcg_hash(seed)) / float(0xFFFFFFFFu);
}
