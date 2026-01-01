#pragma once

#include <string>
#include <vector>

std::vector<char> read_binary_file(const std::string &filepath);

// 计算单个字符串的 32 位 hash 值（使用 FNV-1a 算法）
uint32_t hash_string(const char *s);

// 计算两个字符串的 32 位 hash 值（使用 FNV-1a 算法）
uint32_t hash_strings(const char *a, const char *b);
