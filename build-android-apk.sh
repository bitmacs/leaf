#!/bin/bash

set -e

pushd org.libsdl.hello
./gradlew assembleDebug
popd
