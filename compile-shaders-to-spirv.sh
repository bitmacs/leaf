#!/bin/bash

set -e

mkdir -p org.libsdl.hello/app/src/main/assets/shaders

pushd shaders > /dev/null
# 编译 shader 并输出到 assets 目录，这样 APK 打包时会包含这些文件
glslc triangle.vert -o ../org.libsdl.hello/app/src/main/assets/shaders/triangle.vert.spv
glslc triangle.frag -o ../org.libsdl.hello/app/src/main/assets/shaders/triangle.frag.spv
popd > /dev/null
