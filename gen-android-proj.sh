#!/bin/bash

set -e

pushd external/SDL
./build-scripts/create-android-project.py --variant=symlink --output ../.. org.libsdl.hello docs/hello.c
popd
