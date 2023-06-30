#! /usr/bin/bash

LIBFLAN=/usr/local/lib/x86_64-linux-gnu/libflan.a # should be able to replace with -lflan
FILES=block_count.c
TARGET=block_count
gcc $FILES $LIBFLAN $(pkg-config --libs xnvme) -o $TARGET -Wall -g

backing_file="$(mktemp)_flexnvme"

# Block size cannot go above 4096
block_size=4096
#block_size=2048

block_count=64
slab_block_count=32

echo "Creating backing file: $backing_file"

sudo dd if=/dev/zero of=$backing_file bs=$block_size count=$block_count
sudo losetup -fP -b $block_size $backing_file
loopback_device=$(losetup --output=NAME -j "$backing_file" | tail -1)

if [ -z "$loopback_device" ]; then
  echo "Failed to find loopback device"
  sudo rm $backing_file
  exit 1
fi

sudo mkfs.flexalloc -s $slab_block_count $loopback_device
echo ./$TARGET $loopback_device $block_size
#sudo gdb ./$TARGET $loopback_device $block_size
sudo ./$TARGET $loopback_device $block_size

sudo losetup -d $loopback_device
sudo rm $backing_file
