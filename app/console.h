#pragma once

// Small console helpers for the Ratrix CLI: ANSI colors, confirmation prompts,
// and a no-echo passphrase reader so secrets never render on screen.

#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

// Colors are macros so adjacent string-literal concatenation (e.g. GREY "text")
// works at the call sites.
#define RESET   "\033[0m"
#define DIM     "\033[2m"
#define BOLD    "\033[1m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"
#define GREY    "\033[90m"

namespace con {

// Enable ANSI escape processing on Windows 10+ consoles.
inline void enableAnsi() {
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode)) SetConsoleMode(h, mode | 0x0004 /*ENABLE_VIRTUAL_TERMINAL_PROCESSING*/);
    SetConsoleOutputCP(CP_UTF8);
#endif
}

inline std::string readSecret(const std::string& prompt) {
    std::cout << prompt << std::flush;
    std::string out;
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(h, &mode);
    SetConsoleMode(h, mode & ~ENABLE_ECHO_INPUT);
    std::getline(std::cin, out);
    SetConsoleMode(h, mode);
#else
    termios oldt{};
    tcgetattr(STDIN_FILENO, &oldt);
    termios noecho = oldt;
    noecho.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &noecho);
    std::getline(std::cin, out);
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
#endif
    std::cout << "\n";
    return out;
}

inline bool confirm(const std::string& prompt) {
    std::cout << prompt << " [y/N]: " << std::flush;
    std::string line;
    std::getline(std::cin, line);
    return line == "y" || line == "Y" || line == "yes";
}

}  // namespace con
