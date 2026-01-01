cmake_minimum_required(VERSION 3.6)

project(GAME)

# 全局禁用 RTTI，应用于所有 native 代码
add_compile_options(-fno-rtti)

# SDL sources are in a subfolder named "SDL"
add_subdirectory(SDL)
add_subdirectory(glm)

# Compilation of companion libraries
#add_subdirectory(SDL_image)
#add_subdirectory(SDL_mixer)
#add_subdirectory(SDL_ttf)

# Your game and its CMakeLists.txt are in a subfolder named "src"
add_subdirectory(src)

