#!/bin/bash

echo "🔥 Building..."
rm -rf build
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

echo ""
echo "✅ Built: ./voice"
echo ""
echo "📡 Your IP:"
ip -4 addr show | grep -oP '(?<=inet\s)\d+(\.\d+){3}' | grep -v 127.0.0.1
echo ""
echo "🔄 Quick test:"
echo "  Terminal 1: ./voice server"
echo "  Terminal 2: ./voice client <IP_ABOVE>"
