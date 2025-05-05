#!/bin/bash

BIN_DIR=../../usr/bin
OUTPUT=../../sys/ktfs.raw
SIZE=32M
INODES=32

./mkfs_ktfs "$OUTPUT" "$SIZE" "$INODES" $(find "$BIN_DIR")
