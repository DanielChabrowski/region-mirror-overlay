#!/usr/bin/env bash

mkdir -p bin
g++ -O2 mirror_overlay.cpp -o bin/mirror-overlay -lxcb -lxcb-shape -lxcb-xfixes -lxcb-render
