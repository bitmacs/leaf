#!/bin/bash

set -ex

CLEAN=false
BUILD=false
PURGE=false

# 手动解析参数
while [[ $# -gt 0 ]]; do
    case $1 in
        -c|--clean)
            CLEAN=true
            shift
            ;;
        -b|--build)
            BUILD=true
            shift
            ;;
        -p|--purge)
            PURGE=true
            shift
            ;;
        *)
            echo "未知选项: $1" >&2
            exit 1
            ;;
    esac
done

if [ "$CLEAN" = true ]; then
    pushd org.libsdl.hello > /dev/null
    ./gradlew clean
    popd > /dev/null
fi

if [ "$PURGE" = true ]; then
    pushd org.libsdl.hello > /dev/null
    rm -rf .gradle
    rm -rf build
    rm -rf app/.cxx
    rm -rf app/build
    popd > /dev/null
fi

if [ "$BUILD" = true ]; then
    pushd org.libsdl.hello > /dev/null
    ./gradlew assembleDebug
    popd > /dev/null

    # 输出 APK 路径
    APK_RELATIVE_PATH="org.libsdl.hello/app/build/outputs/apk/debug/app-debug.apk"
    if [ -f "$APK_RELATIVE_PATH" ]; then
        APK_ABSOLUTE_PATH="$(pwd)/$APK_RELATIVE_PATH"
        echo ""
        echo "✓ APK 构建成功！"
        echo "  绝对路径: $APK_ABSOLUTE_PATH"
        echo "  相对路径: $APK_RELATIVE_PATH"
    else
        echo "警告: 未找到 APK 文件: $APK_RELATIVE_PATH" >&2
    fi
fi
