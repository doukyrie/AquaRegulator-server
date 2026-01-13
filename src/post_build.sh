#!/bin/sh

filename=$(readlink -f "$0")
dir=$(dirname "$filename")

set -e
printf "\033[32mhello from post build script! \033[0m\n"

echo "export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:${PWD}"

if [ -e "../build/libhpsocket.so" ]; then
    printf "\033[32mCreating symlink for libhpsocket.so.6 \033[0m\n"
    exit 0
fi

if [ -e "../3rdparty/linux/hp-socket-6.0.7-lib-linux/lib/hpsocket/x64/libhpsocket.so" ]; then
    cp ../3rdparty/linux/hp-socket-6.0.7-lib-linux/lib/hpsocket/x64/libhpsocket.so ../build
    ln -s ${dir}/../build/libhpsocket.so ${dir}/../build/libhpsocket.so.6
    printf "\033[32mCopied libhpsocket.so to AquaRegS directory. \033[0m\n"
else
    printf "\033[31mlibhpsocket.so not found! \033[0m\n"
    exit 1
fi