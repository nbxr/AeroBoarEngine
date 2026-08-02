#include "AeroBoar.h"
#include "core/Configuration.h"
#include "core/Log.h"
#include <iostream>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>

// Duplicate a message to both the (possibly redirected) C++ streams and
// the debugger Output window (View > Output > Debug in VS Code).
// This helps a lot when the console attachment fights the IDE terminal.
static void log_line(const char* msg) {
    std::cout << msg << std::endl;
    std::cout.flush();
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
}

static void log_line(const std::string& msg) {
    log_line(msg.c_str());
}

void attach_console_for_standalone() {
    // This exists so that double-clicking the .exe from Explorer gives you
    // a console with output. It is deliberately conservative:
    // - If launched from cmd/PowerShell, AttachConsole usually succeeds and
    //   we reuse the parent terminal.
    // - If there is no console at all (typical for pure GUI launch), we
    //   AllocConsole() which creates a new window titled "AeroBoar Console".
    // - When running inside VS Code (debugger or integrated terminal),
    //   this often creates a SEPARATE window or redirects in a way that
    //   makes output invisible in the VS Code "Terminal" tabs. That's why
    //   you currently can't see "Flying with AeroBoar".
    //
    // For day-to-day development in VS Code it is often better to:
    //   1. Launch the exe from a real PowerShell/cmd inside the build folder, or
    //   2. Temporarily comment out the attach() call below, or
    //   3. Set "console": "externalTerminal" in your launch.json.

    BOOL attached = AttachConsole(ATTACH_PARENT_PROCESS);
    if (!attached && !GetConsoleWindow()) {
        AllocConsole();
        SetConsoleTitleA("AeroBoar Console");
    }

    // Redirect the C stdio streams to the console device.
    // We ignore failures here on purpose — we also use OutputDebugStringA.
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    freopen("CONIN$", "r", stdin);

    // Make sure C++ iostreams are talking to the new C streams.
    std::ios::sync_with_stdio(true);
    std::cout.clear();
    std::cerr.clear();
}
#elif defined(__linux__)
// Linux can usually just inherit the terminal. Add support here if needed.
#endif

int main() {
    try {
#ifdef _WIN32
        attach_console_for_standalone();
#endif
        log_line("Flying with AeroBoar");

        // Enable persistent flushed log file early.
        // It is created relative to the process current working directory
        // (which is set to the build folder under the VS debugger via
        // VS_DEBUGGER_WORKING_DIRECTORY in CMakeLists.txt).
        // The full absolute path is printed to the console (and also to the
        // debugger Output window on Windows via the log_line / OutputDebugString path).
        std::string log_path = core::init_file_log("aero_boar.log");
#ifdef _WIN32
        {
            std::string announcement = "[Log] Writing diagnostics + Vulkan validation to: " + log_path;
            OutputDebugStringA(announcement.c_str());
            OutputDebugStringA("\n");
        }
#endif

        if (!core::Configuration::get_instance().load()) {
            log_line("FATAL: Failed to load assets/scenes/configuration.json");
            core::close_file_log();
            return -1;
        }

        AeroBoar aero_boar;
        int ret = aero_boar.fly();

        // Keep the console window open if we created one and the user
        // launched by double-click (common for standalone runs).
#ifdef _WIN32
        if (GetConsoleWindow()) {
            log_line("Press Enter to close this console...");
            std::cin.get();
        }
#endif
        core::close_file_log();
        return ret;
    } catch (const std::runtime_error &e) {
        std::string msg = std::string("FATAL: ") + e.what();
        std::cerr << msg << std::endl;
#ifdef _WIN32
        OutputDebugStringA(msg.c_str());
        OutputDebugStringA("\n");
        if (GetConsoleWindow()) {
            std::cerr << "Press Enter to close this console...";
            std::cin.get();
        }
#endif
        core::close_file_log();
        return -1;
    }
    core::close_file_log();
    return 0;
}