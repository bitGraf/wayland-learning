#!/usr/bin/bash

if [[ "$1" == "no_colors" ]]; then
	S=''
	E=''
else
	S='\033[1;33m'
	E='\033[0m'
fi

# clean directory structure
echo -e "$S[Cleaning build environment]$E"
rm -rf lbuild
rm -rf bin

