#pragma once
#include <string>
#include <iostream>

// ANSI color codes for the CLI. Matches the palette used in the Android
// client (cyan/amber/green/pink) so the two feel like the same product.
//
// On Windows, modern cmd.exe/PowerShell (Windows 10+) support ANSI escape
// codes but don't interpret them by default - call enableAnsiOnWindows()
// once at program startup (see main()) to turn that on. Linux terminals
// interpret these natively with no setup needed.
namespace Color {
    constexpr const char* RESET   = "\033[0m";
    constexpr const char* BOLD    = "\033[1m";

    constexpr const char* CYAN    = "\033[36m";
    constexpr const char* BOLD_CYAN = "\033[1;36m";
    constexpr const char* AMBER   = "\033[33m";
    constexpr const char* GREEN   = "\033[32m";
    constexpr const char* BOLD_GREEN = "\033[1;32m";
    constexpr const char* RED     = "\033[31m";
    constexpr const char* BOLD_RED = "\033[1;31m";
    constexpr const char* PINK    = "\033[35m";
    constexpr const char* BLUE    = "\033[34m";
    constexpr const char* GRAY    = "\033[90m";
}

// Enables ANSI escape sequence interpretation in the Windows console.
// No-op on Linux (already works natively there).
#ifdef _WIN32
  #include <windows.h>
  inline void enableAnsiOnWindows() {
      HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
      if (hOut == INVALID_HANDLE_VALUE) return;
      DWORD mode = 0;
      if (!GetConsoleMode(hOut, &mode)) return;
      SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
  }
#else
  inline void enableAnsiOnWindows() { /* no-op on Linux */ }
#endif

// A bold, colored section banner shown when entering a menu choice - e.g.
// "== ADD ORDER ==" - so it's always visually obvious which screen you're on.
inline void printSectionTitle(const std::string& title) {
    std::cout << "\n" << Color::BOLD_CYAN << "== " << title << " =="
               << Color::RESET << "\n";
}

// Wraps a field prompt label in amber, used for all data-entry prompts.
inline void printLabel(const std::string& text) {
    std::cout << Color::AMBER << text << Color::RESET;
}

// Prints an OK|... or ERR|... server response in green/red respectively.
inline void printResponse(const std::string& resp) {
    if (resp.rfind("OK", 0) == 0) {
        std::cout << Color::GREEN << resp << Color::RESET << "\n";
    } else if (resp.rfind("ERR", 0) == 0) {
        std::cout << Color::BOLD_RED << resp << Color::RESET << "\n";
    } else {
        std::cout << resp << "\n";
    }
}
