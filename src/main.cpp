#include "config.h"
#include "tweaks/definitive_driveby.h"
#include "tweaks/draw_cols.h"
#include "tweaks/minor.h"

#ifndef NOMINMAX
  #define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

static auto GetErrorAsString(DWORD errorCode) -> std::string {
    LPSTR messageBuffer = nullptr;

    const auto messageLen = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    nullptr, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPSTR>(&messageBuffer), 0, nullptr);
    if (messageLen == 0) {
        const auto errorCode = GetLastError();
        throw std::system_error{static_cast<int>(errorCode), std::system_category(),
            "Failed to retrieve error message string"};
    }

    const auto message = std::string{messageBuffer, messageLen};
    
    LocalFree(messageBuffer);
            
    return message;
}

static auto GetDllPath(HINSTANCE hDll) -> std::filesystem::path {
    WCHAR path[MAX_PATH];

    if (GetModuleFileNameW(hDll, path, MAX_PATH) == 0) {
        const auto message = GetErrorAsString(GetLastError());
        throw std::runtime_error{"Failed to get DLL path: " + message};
    }
    
    return std::filesystem::path{path};
}

extern BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID /*reserved*/) {
    if (reason != DLL_PROCESS_ATTACH) {
        return TRUE;
    }

    auto configPath = GetDllPath(instance);
    configPath.replace_extension("toml");

    const auto config = Config::ParseFile(configPath);
    if (!config) {
        const auto caption = std::format("Unable to parse '{}'", configPath.filename().string());
        MessageBoxA(nullptr, config.error().c_str(), caption.c_str(), MB_OK | MB_ICONWARNING);
        return FALSE;
    }

    try {
        definitive_driveby::ReadConfig(*config);
        draw_cols::ReadConfig(*config);
        minor_tweaks::ReadConfig(*config);
    } catch (const std::exception& e) {
        const auto caption = std::format("Unable to deserialize '{}'", configPath.filename().string());
        MessageBoxA(nullptr, e.what(), caption.data(), MB_OK | MB_ICONERROR);
        exit(EXIT_FAILURE);
        return FALSE;
    }

    definitive_driveby::Apply();
    draw_cols::Apply();
    minor_tweaks::Apply();

    return TRUE;
}
