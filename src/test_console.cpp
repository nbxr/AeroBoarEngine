#include <iostream>
#include <windows.h>

inline void attach() {
    std::cout << "Attempting to attach console..." << std::endl;
    
    // Attempt to attach to the parent process console
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        std::cout << "Failed to attach to parent console, allocating new console..." << std::endl;
        if (!AllocConsole()) {
            std::cout << "Failed to allocate console!" << std::endl;
            return;
        }
    }
    
    std::cout << "Console window handle: " << GetConsoleWindow() << std::endl;
    
    // Redirect standard streams to the console
    std::cout << "Redirecting stdout..." << std::endl;
    FILE* fp = freopen("CONOUT$", "w", stdout);
    std::cout << "freopen stdout result: " << (fp ? "success" : "failed") << std::endl;
    
    std::cout << "Redirecting stderr..." << std::endl;
    fp = freopen("CONOUT$", "w", stderr);
    std::cout << "freopen stderr result: " << (fp ? "success" : "failed") << std::endl;
    
    std::cout << "Redirecting stdin..." << std::endl;
    fp = freopen("CONIN$", "r", stdin);
    std::cout << "freopen stdin result: " << (fp ? "success" : "failed") << std::endl;
    
    // Set console title
    SetConsoleTitleA("Test Console");
    std::cout << "Console attached successfully!" << std::endl;
}

int main() {
    attach();
    
    std::cout << "\n\n=== Testing Output ===" << std::endl;
    std::cerr << "=== Testing Error Output ===" << std::endl;
    
    std::string input;
    std::cout << "Enter something: ";
    std::getline(std::cin, input);
    
    std::cout << "You entered: " << input << std::endl;
    
    return 0;
}