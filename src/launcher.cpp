#include <windows.h>
#include <detours.h>
#include <pathcch.h>
#include <string>
#include "shared.h"

//******************//
//      GLOBALS     //
//******************//

static std::wstring REG_KEY_INSTALL_PATH = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\VB:Voicemeeter {17359A74-1236-5467}";
static std::string DLL_NAME = "vmtheme" ARCH_POSTFIX ".dll";

//******************//
//   VM NAMES x64   //
//******************//

#if defined(_WIN64) && defined(FLAVOR_DEFAULT)
static std::wstring EXECUTABLE_NAME = L"voicemeeter_x64.exe";
#endif

#if defined(_WIN64) && defined(FLAVOR_BANANA)
static std::wstring EXECUTABLE_NAME = L"voicemeeterpro_x64.exe";
#endif

#if defined(_WIN64) && defined(FLAVOR_POTATO)
static std::wstring EXECUTABLE_NAME = L"voicemeeter8x64.exe";
#endif

//******************//
//   VM NAMES x86   //
//******************//

#if !defined(_WIN64) && defined(FLAVOR_DEFAULT)
static std::wstring EXECUTABLE_NAME = L"voicemeeter.exe";
#endif

#if !defined(_WIN64) && defined(FLAVOR_BANANA)
static std::wstring EXECUTABLE_NAME = L"voicemeeterpro.exe";
#endif

#if !defined(_WIN64) && defined(FLAVOR_POTATO)
static std::wstring EXECUTABLE_NAME = L"voicemeeter8.exe";
#endif

//******************//
// HELPER FUNCTIONS //
//******************//

/**
 * Reads the install path of Voicemeeter from the registry. Should default to "C:\Program Files (x86)\VB\Voicemeeter"
 * Adapted from https://github.com/vburel2018/Voicemeeter-SDK/blob/main/example0/vmr_client.c#L80
 * @return The install path
 */
std::wstring get_install_path()
{
    HKEY phkResult;
    DWORD size;

    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, REG_KEY_INSTALL_PATH.c_str(), 0, KEY_READ | KEY_WOW64_32KEY, &phkResult) != ERROR_SUCCESS)
        error(L"error RegOpenKeyExW");

    if ((RegQueryValueEx(phkResult, L"UninstallString", nullptr, nullptr, nullptr, &size)))
        error(L"error RegQueryValueExW");

    std::wstring path(size, L'\0');

    if ((RegQueryValueEx(phkResult, L"UninstallString", nullptr, nullptr, reinterpret_cast<LPBYTE>(path.data()), &size)))
        error(L"error RegQueryValueExW");

    if (PathCchRemoveFileSpec(path.data(), path.size()) != S_OK)
        error(L"error PathCchRemoveFileSpec");

    return path;
}

/**
 * Spawns a Voicemeeter process that loads the theme DLL
 */
void spawn_proc()
{
    std::wstring install_path = get_install_path();
    std::wstring full_path_process(MAX_PATH, L'\0');
    std::wstring full_path_dll(MAX_PATH, L'\0');

    if (PathCchCombine(full_path_process.data(), full_path_process.size() + 1, install_path.c_str(), EXECUTABLE_NAME.c_str()) != S_OK)
        error(L"error PathCchCombine");

    if (PathCchCombine(full_path_dll.data(), full_path_dll.size() + 1, install_path.c_str(), str_to_wstr(DLL_NAME).c_str()) != S_OK)
        error(L"error PathCchCombine");

    if (GetFileAttributes(full_path_dll.c_str()) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND)
        error(L"error vmtheme.dll not found");

    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {nullptr};

    BOOL success = DetourCreateProcessWithDllEx(
        full_path_process.c_str(),
        nullptr,
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        install_path.c_str(),
        &si,
        &pi,
        DLL_NAME.c_str(),
        nullptr
    );

    if (!success)
        error(L"error DetourCreateProcessWithDllExW");
}

/**
 * Main function
 */
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
    spawn_proc();
    return 0;
}
