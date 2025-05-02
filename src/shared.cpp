#include <windows.h>
#include <string>

/**
* Displays a messagebox with OK button
 * @param msg The message to be displayed
 */
void mbox(const std::wstring& msg)
{
    MessageBox(nullptr, msg.c_str(), nullptr, MB_ICONWARNING | MB_OK);
}

/**
 * Create a messagebox with the error message and then terminate with code 1
 * @param msg The message to be displayed
 */
void error(const std::wstring& msg)
{
    mbox(msg);
    exit(1);
}

/**
 * Allocate console to print debug messages
 */
void attach_console()
{
    mbox(L"attached");
    if (!AllocConsole())
        error(L"error AllocConsole");

    FILE* fDummy;
    if (freopen_s(&fDummy, "CONOUT$", "w", stdout) != ERROR_SUCCESS)
        error(L"error freopen_s");
}

/**
 * Converts a UTF-8 encoded std::string to std::wstring to be used for WinAPI
 * @param str Narrow string for conversion
 * @return Converted wide string
 */
std::wstring str_to_wstr(const std::string& str)
{
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
    if (size == 0)
        error(L"error MultiByteToWideChar");

    std::wstring res(size, 0);
    if (MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), res.data(), size) == 0)
        error(L"error MultiByteToWideChar");

    return res;
}
