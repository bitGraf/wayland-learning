#!/usr/bin/bash

# setup directory structure
# mkdir -p lbuild

# setup build variables
# generator="Unix Makefiles"
export CC=clang
export CXX=clang++
generator="Ninja"

if [[ "$1" == "no_colors" ]]; then
	S=''
	E=''
else
	S='\033[1;33m'
	E='\033[0m'
fi

# generate wayland headers/source
echo -e "$S[Generate wayland headers]$E"
wayland-scanner client-header < /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml > app/include/xdg/xdg-shell-client-protocol.h
wayland-scanner private-code < /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml > app/src/xdg/xdg-shell-protocol.c

# cmake -S . -B lbuild -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
echo -e "$S[Configuring]$E"
cmake -G "$generator" -S . -B lbuild -DCMAKE_BUILD_TYPE=Debug
# cmake -S . -B lbuild -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug		# build with unix makefile
# cmake -G "Ninja" -S . -B lbuild -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug		# build with clang
# cmake -G "Ninja" -S . -B lbuild -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Debug			# build with gcc
echo -e "$S[Building]$E"
cmake --build lbuild
#echo "[Installing]"
#cmake --install lbuild
