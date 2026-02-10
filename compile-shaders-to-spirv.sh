#!/bin/bash

set -e

mkdir -p org.libsdl.hello/app/src/main/assets/shaders

pushd shaders > /dev/null
# 编译 shader 并输出到 assets 目录，这样 APK 打包时会包含这些文件
# -I. 指定 include 目录为当前目录（shaders/），这样 #include "types.glsl" 能找到文件
glslc -I. triangle.vert -o ../org.libsdl.hello/app/src/main/assets/shaders/triangle.vert.spv
glslc -I. triangle.frag -o ../org.libsdl.hello/app/src/main/assets/shaders/triangle.frag.spv
glslc -I. picking.vert -o ../org.libsdl.hello/app/src/main/assets/shaders/picking.vert.spv
glslc -I. picking.frag -o ../org.libsdl.hello/app/src/main/assets/shaders/picking.frag.spv
glslc -I. gbuffer.vert -o ../org.libsdl.hello/app/src/main/assets/shaders/gbuffer.vert.spv
glslc -I. gbuffer.frag -o ../org.libsdl.hello/app/src/main/assets/shaders/gbuffer.frag.spv
glslc -I. path_tracing.comp -o ../org.libsdl.hello/app/src/main/assets/shaders/path_tracing.comp.spv
popd > /dev/null
