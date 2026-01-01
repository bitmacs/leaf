#include "files.h"
#include <SDL3/SDL_iostream.h>
#include <cassert>

std::vector<char> read_binary_file(const std::string &filepath) {
    // 使用 SDL3 的 SDL_LoadFile 读取文件，在 Android 上可以自动处理 APK 中的资源
    // SDL_LoadFile 会自动从 assets 目录查找文件
    size_t data_size = 0;
    void *data = SDL_LoadFile(filepath.c_str(), &data_size);
    assert(data != nullptr && data_size > 0);

    // 将数据复制到 vector 中
    std::vector<char> buffer(static_cast<const char *>(data), static_cast<const char *>(data) + data_size);

    SDL_free(data);

    return buffer;
}

uint32_t hash_string(const char *s) {
    uint32_t hash = 2166136261u; // FNV-1a offset basis (32-bit)
    const uint32_t prime = 16777619u; // FNV-1a prime (32-bit)

    while (*s != '\0') {
        hash ^= static_cast<uint32_t>(*s);
        hash *= prime;
        ++s;
    }

    return hash;
}

uint32_t hash_strings(const char *a, const char *b) {
    const uint32_t separator = 0x1F; // 使用不可打印字符作为分隔符

    uint32_t hash = 2166136261u; // FNV-1a offset basis (32-bit)
    const uint32_t prime = 16777619u; // FNV-1a prime (32-bit)

    // hash 第一个字符串
    while (*a != '\0') {
        hash ^= static_cast<uint32_t>(*a);
        hash *= prime;
        ++a;
    }

    // hash 分隔符（明确区分两个字符串的边界）
    hash ^= separator;
    hash *= prime;

    // hash 第二个字符串
    while (*b != '\0') {
        hash ^= static_cast<uint32_t>(*b);
        hash *= prime;
        ++b;
    }

    return hash;
}
