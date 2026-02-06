cmake_minimum_required(VERSION 3.6)

project(GAME)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON) # 确保使用 C++17 标准

# 全局禁用 RTTI，应用于所有 native 代码
add_compile_options(-fno-rtti)

# SDL sources are in a subfolder named "SDL"
add_subdirectory(SDL)
add_subdirectory(glm)

# Compilation of companion libraries
#add_subdirectory(SDL_image)
#add_subdirectory(SDL_mixer)
#add_subdirectory(SDL_ttf)

add_library(cgltf INTERFACE)
target_include_directories(cgltf INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/cgltf)
target_compile_definitions(cgltf INTERFACE CGLTF_IMPLEMENTATION CGLTF_WRITE_IMPLEMENTATION)

# Your game and its CMakeLists.txt are in a subfolder named "src"
add_subdirectory(src)

