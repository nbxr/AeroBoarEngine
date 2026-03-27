#include "AeroBoar.h"
#include <iostream>

#ifdef _WIN32
#include <stdio.h>
#include <string>
#include <windows.h>

void attach() {
    // Attempt to attach to the parent process console
    AttachConsole(ATTACH_PARENT_PROCESS);

    // If that fails, allocate a new console window
    if (!GetConsoleWindow()) {
        AllocConsole();
    }
    
    // Redirect standard streams to the console
    // Check if streams were redirected successfully
    if (freopen("CONOUT$", "w", stdout) == nullptr) {
        // Fallback if freopen fails
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        SetStdHandle(STD_OUTPUT_HANDLE, hConsole);
    }

    if (freopen("CONOUT$", "w", stderr) == nullptr) {
        HANDLE hConsole = GetStdHandle(STD_ERROR_HANDLE);
        SetStdHandle(STD_ERROR_HANDLE, hConsole);
    }

    // For stdin, if we're running under another process, we might not have
    // input access so we redirect to a dummy file or ignore it
    if (freopen("CONIN$", "r", stdin) == nullptr) {
        // stdin redirection might fail if we can't access it
        // This is okay - we'll just not read input
    }

    // Set console title
    SetConsoleTitleA("AeroBoar Console");
}
#endif

int main() {
    try {
#ifdef _WIN32
        attach();
#endif
        AeroBoar aero_boar;
        std::cout << "Running AeroBoar" << std::endl;
        std::string line;
        std::getline(std::cin, line);

        return aero_boar.fly();
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return -1;
    }
    return 0;
}
