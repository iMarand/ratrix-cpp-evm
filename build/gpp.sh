#!/bin/bash


# g++ "$1" -o "${2:-app.exe}" $(pkg-config --cflags --libs Qt6Widgets Qt6Gui Qt6Core Qt6Network Qt6Multimedia Qt6MultimediaWidgets Qt6Sql libcurl openssl) -I/mingw64/include/boost -L/mingw64/lib -lboost_thread-mt -lboost_filesystem-mt -pthread -lpthread -lshell32 -lpcre2-8 -lprotobuf -lsqlite3
# g++ app.cpp -o app.exe $(pkg-config --cflags --libs drogon)


# g++ "$1" -o "${2:-app}" -IC:/msys64/mingw64/include/qt6/QtWidgets -IC:/msys64/mingw64/include/qt6 -DQT_WIDGETS_LIB -IC:/msys64/mingw64/include/qt6/QtGui -DQT_GUI_LIB -IC:/msys64/mingw64/include/qt6/QtNetwork -DQT_NETWORK_LIB -IC:/msys64/mingw64/include/qt6/QtSql -DQT_SQL_LIB -IC:/msys64/mingw64/include/qt6/QtCore -DQT_CORE_LIB -IC:/msys64/mingw64/share/qt6/mkspecs/win32-g++ -DWIN32 -D_ENABLE_EXTENDED_ALIGNED_STORAGE -DWIN64 -D_WIN64 -DMINGW_HAS_SECURE_API=1 -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -lQt6Widgets -lQt6Gui -lQt6Network -lQt6Sql -lQt6Core -L/mingw64/lib -lcurl -lssl -lcrypto -luser32 -lws2_32 -lboost_thread-mt -lboost_filesystem-mt -pthread
# g++ "$1" -o "${2:-app}" -IC:/msys64/mingw64/include/qt6/QtWidgets -IC:/msys64/mingw64/include/qt6 -DQT_WIDGETS_LIB -IC:/msys64/mingw64/include/qt6/QtGui -DQT_GUI_LIB -IC:/msys64/mingw64/include/qt6/QtNetwork -DQT_NETWORK_LIB -IC:/msys64/mingw64/include/qt6/QtSql -DQT_SQL_LIB -IC:/msys64/mingw64/include/qt6/QtCore -DQT_CORE_LIB -IC:/msys64/mingw64/share/qt6/mkspecs/win32-g++ -DWIN32 -D_ENABLE_EXTENDED_ALIGNED_STORAGE -DWIN64 -D_WIN64 -DMINGW_HAS_SECURE_API=1 -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -lQt6Widgets -lQt6Gui -lQt6Network -lQt6Sql -lQt6Core -L/mingw64/lib -lcurl -lssl -lcrypto -luser32 -lws2_32 -lboost_thread-mt -lboost_filesystem-mt -pthread


# g++ "$1" -o "${2:-app}" -lcurl -lssl -lcrypto -I/mingw64/include -L/mingw64/lib





# ---------------------------------Build Scratch (Debug Mode Added) -------------------------------------------------------


if [ $# -lt 1 ]; then
    echo "Usage: $0 <source1.cpp> [source2.cpp ...] [output_name|--debug]"
    exit 1
fi

DEBUG_MODE=false
SRC_ARRAY=()

for arg in "$@"; do
    if [[ "$arg" == "--debug" ]]; then
        DEBUG_MODE=true
    elif [[ "$arg" == *" "* ]]; then
        read -ra SPLIT <<< "$arg"
        SRC_ARRAY+=("${SPLIT[@]}")
    else
        SRC_ARRAY+=("$arg")
    fi
done

if [ "$DEBUG_MODE" = true ]; then
    OUT="/tmp/debug_$(basename "${SRC_ARRAY[0]}" .cpp)_$$"
    SRC=("${SRC_ARRAY[@]}")
else
    OUT="${SRC_ARRAY[-1]}"
    SRC=("${SRC_ARRAY[@]:0:${#SRC_ARRAY[@]}-1}")
fi

# PKG_FLAGS=$(pkg-config --cflags --libs Qt6Widgets Qt6Gui Qt6Core Qt6Network Qt6Multimedia Qt6MultimediaWidgets Qt6Sql libcurl openssl protobuf absl_base absl_log_internal_message 2>/dev/null)
# EXTRA_FLAGS="-I/mingw64/include/boost -L/mingw64/lib"
# MANUAL_LIBS="-lboost_thread-mt -lboost_filesystem-mt -lpcre2-8 -pthread -lshell32 -lsqlite3"

PKG_FLAGS=$(pkg-config --cflags --libs Qt6Widgets Qt6Gui Qt6Core Qt6Network Qt6Multimedia Qt6MultimediaWidgets Qt6Sql libcurl openssl protobuf grpc++ grpc absl_base absl_log_internal_message 2>/dev/null)
EXTRA_FLAGS="-I/mingw64/include/boost -I/mingw64/include/grpcpp -L/mingw64/lib"
MANUAL_LIBS="-lboost_thread-mt -lboost_filesystem-mt -lgrpc++ -lgrpc -lgrpc++_reflection -lpcre2-8 -pthread -lshell32 -lsqlite3"

if [ "$DEBUG_MODE" = true ]; then
    EXTRA_FLAGS="$EXTRA_FLAGS -g"
    echo "==============================================================================="
    echo "|| 🐛 Debug mode enabled"
fi

echo "|| 🔧 Compiling sources: ${SRC[*]}"
echo "|| ➡️ Output: $OUT"

g++ -std=c++17 "${SRC[@]}" -o "$OUT" $PKG_FLAGS $EXTRA_FLAGS $MANUAL_LIBS

if [ $? -eq 0 ]; then
    echo "|| ✅ Build successful: $OUT"
    
    if [ "$DEBUG_MODE" = true ]; then
        echo "|| 🚀 Running in debug mode..."
        echo "==============================================================================="
        echo ""

        "$OUT"
        EXIT_CODE=$?
        
        rm -f "$OUT"
        
        echo ""
        echo "🏁 Program exited with code: $EXIT_CODE"
        exit $EXIT_CODE
    fi
else
    echo "❌ Build failed"
    exit 1
fi
