#!/bin/bash

echo "=== Building main.exe ==="
g++ main.cpp -o main.exe -static-libgcc -static-libstdc++ -lcurl -lssl -lcrypto -lws2_32 -lcrypt32 -lwldap32 -lz

if [ $? -ne 0 ]; then
    echo "Build failed!"
    exit 1
fi

echo ""
echo "=== Checking dependencies ==="
ldd main.exe

echo ""
echo "=== Copying required DLLs ==="

# Create output directory
mkdir -p bundle
cp main.exe bundle/

# Extract and copy DLLs from mingw64/bin
ldd main.exe | grep "=> /mingw64" | awk '{print $3}' | while read dll; do
    if [ -f "$dll" ]; then
        cp -v "$dll" bundle/
    fi
done

echo ""
echo "=== Bundle complete ==="
echo "All files are in the 'bundle' directory"
ls -lh bundle/