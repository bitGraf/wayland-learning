#! /usr/bin/bash

mkdir -p lbuild
# cmake -S . -B lbuild -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
echo "[Configuring]"
cmake -G "Ninja" -S . -B lbuild -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug
echo "[Building]"
cmake --build lbuild
#echo "[Installing]"
#cmake --install lbuild
