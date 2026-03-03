@echo off
g++ main.cpp -o main.exe -lcurl -lssl -lcrypto -lws2_32 -lcrypt32 -lwldap32 -lz
ldd main.exe | grep "=> /mingw64" | awk '{print $3}' | xargs -I {} cp {} .