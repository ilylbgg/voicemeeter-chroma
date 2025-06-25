/**
Copyright (C) 2025 Klaus Hahnenkamp

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <windows.h>
#include <windowsx.h>
#include <psapi.h>
#include <string>
#include <optional>
#include <vector>
#include <shlwapi.h>
#include <filesystem>
#include <shlobj.h>
#include "utils.hpp"

#include <fstream>
#include <sstream>

#include "spdlog/spdlog.h"
#include "spdlog/sinks/rotating_file_sink.h"

#include <capstone/capstone.h>

#include "detours.h"

namespace utils
{
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
void mbox_error(const std::wstring& msg)
{
    mbox(L"error: " + msg);
    exit(1);
}

/**
 * Converts a UTF-8 encoded std::string to std::wstring to be used for WinAPI
 * @param str Narrow string for conversion
 * @return Converted wide string
 */
std::optional<std::wstring> str_to_wstr(const std::string& str)
{
    const int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
    if (size == 0)
    {
        SPDLOG_ERROR("failed to convert string to wstring");
        return std::nullopt;
    }

    std::wstring res(size, 0);
    if (MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), res.data(), size) == 0)
    {
        SPDLOG_ERROR("failed to convert string to wstring");
        return std::nullopt;
    }

    return res;
}

/**
 * Converts a std::wstring to UTF-8 encoded std::string
 * @param wstr Wide string for conversion
 * @return Converted narrow string
 */
std::optional<std::string> wstr_to_str(const std::wstring& wstr)
{
    const int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size == 0)
    {
        SPDLOG_ERROR("failed to convert string to wstring");
        return std::nullopt;
    }

    std::string res(size - 1, 0); // size includes null terminator, subtract 1
    if (WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, res.data(), size, nullptr, nullptr) == 0)
    {
        SPDLOG_ERROR("failed to convert string to wstring");
        return std::nullopt;
    }

    return res;
}

/**
 * Convert COLORREF (BBGGRR) to RGB hex string (#RRGGBB)
 * See https://learn.microsoft.com/en-us/windows/win32/gdi/colorref
 * @param color The color in COLORREF format
 * @return The color as hex string
 */
std::string colorref_to_hex(const COLORREF color)
{
    std::stringstream ss;
    ss << '#' << std::uppercase << std::hex
        << std::setw(2) << std::setfill('0') << static_cast<int>(GetRValue(color))
        << std::setw(2) << std::setfill('0') << static_cast<int>(GetGValue(color))
        << std::setw(2) << std::setfill('0') << static_cast<int>(GetBValue(color));

    return ss.str();
}

/**
 * Convert RGB hex string (#RRGGBB) to COLORREF (BBGGRR)
 * See https://learn.microsoft.com/en-us/windows/win32/gdi/colorref
 * @param hex The color as hex string
 * @return The color in COLORREF format
 */
std::optional<COLORREF> hex_to_colorref(const std::string& hex)
{
    if (hex.empty())
    {
        SPDLOG_ERROR("empty hex value passed");
        return std::nullopt;
    }

    std::string clean_hex = (hex[0] == '#') ? hex.substr(1) : hex;

    if (clean_hex.length() != 6)
    {
        SPDLOG_ERROR("invalid value passed: {}", hex);
        return std::nullopt;
    }

    unsigned long value = 0;

    try
    {
        value = std::stoul(clean_hex, nullptr, 16);
    }
    catch (...)
    {
        SPDLOG_ERROR("invalid hex value passed: {}", clean_hex);
        return std::nullopt;
    }

    const uint8_t r = (value >> 16) & 0xFF;
    const uint8_t g = (value >> 8) & 0xFF;
    const uint8_t b = value & 0xFF;

    return RGB(r, g, b);
}

/**
 * Find non-exported functions using signature scanning
 * Function signatures should be stable across updates
 * Simple O(n*m) implementation
 * @param sig A signature struct containing the byte pattern and a mask
 * @return The absolute address of the function
 */
std::optional<PVOID> find_function_signature(const signature_t& sig)
{
    const auto handle = GetModuleHandle(nullptr);
    MODULEINFO mod_info;

    if (!handle)
    {
        SPDLOG_ERROR("failed to get module handle");
        return std::nullopt;
    }

    if (!GetModuleInformation(GetCurrentProcess(), handle, &mod_info, sizeof(mod_info)))
    {
        SPDLOG_ERROR("failed to get module information");
        return std::nullopt;
    }

    auto start = static_cast<uint8_t*>(mod_info.lpBaseOfDll);
    const size_t end = mod_info.SizeOfImage;
    const size_t pattern_size = sig.pattern.size();
    const uint8_t* pattern = sig.pattern.data();
    const char* mask = sig.mask.data();

    for (size_t i = 0; i < end - pattern_size; i++)
    {
        bool found = true;

        for (size_t j = 0; j < pattern_size; j++)
        {
            if (mask[j] != '?' && pattern[j] != start[i + j])
            {
                found = false;
                break;
            }
        }

        if (found)
            return start + i;
    }

    SPDLOG_ERROR("signature scan exhausted");
    return std::nullopt;
}

/**
 * Loads the bitmap file from the specified path
 * @param path Path to bitmap
 * @param target Target buffer
 * @return True on success
 */
bool load_bitmap(const std::wstring& path, std::vector<uint8_t>& target)
{
    std::ifstream f(path.c_str(), std::ios::binary | std::ios::ate);

    if (!f.is_open())
    {
        SPDLOG_ERROR("failed to open file {}", *wstr_to_str(path));
        return false;
    }

    const auto size = f.tellg();
    f.seekg(0, std::ios::beg);
    target.assign(size, '\0');

    if (!f.read(reinterpret_cast<char*>(target.data()), size))
    {
        SPDLOG_ERROR("failed to read file {}", *wstr_to_str(path));
        return false;
    }

    return true;
}

/**
 * Gets the path to the Voicemeeter user directory
 * @return Path to VM directory
 */
std::optional<std::wstring> get_userprofile_path()
{
    PWSTR buffer = nullptr;

    const auto res = SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &buffer);

    if (res != S_OK)
    {
        SPDLOG_ERROR("SHGetKnownFolderPath failed: {}", res);
        return std::nullopt;
    }

    const std::wstring userprofile_path = buffer;
    std::wstring result = std::filesystem::path(userprofile_path) / L"Voicemeeter";
    CoTaskMemFree(buffer);

    return result;
}

/**
 * Initializes logging library to log to a file
 */
void setup_logging()
{
    const auto userprofile_path_wstr = get_userprofile_path();

    if (!userprofile_path_wstr)
        mbox_error(L"setup_logging: failed to get user profile path");

    auto userprofile_path = wstr_to_str(*userprofile_path_wstr);

    if (!userprofile_path)
        mbox_error(L"setup_logging: string conversion error");

    const std::string log_file_path = (std::filesystem::path(*userprofile_path) / "themes" / "vmchroma_log.txt").string();

    try
    {
        const auto logger = spdlog::rotating_logger_mt("vmchroma_logger", log_file_path, 1048576 * 5, 1);
        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%d.%m.%Y %H:%M:%S] [%l] %s %!:%# %v");
        spdlog::set_level(spdlog::level::err);
        spdlog::flush_on(spdlog::level::err);
    }
    catch (const spdlog::spdlog_ex& ex)
    {
        mbox_error(L"logger setup error:");
    }
}

/**
 * Patches the mulss instructions to NOPs to disable the hardcoded 3x multiplier dB change when scrolling with the mouse wheel
 * @param handler_fn The beginning of the scroll handler function
 * @return True if patches successfully
 */
bool apply_scroll_patch64(o_scroll_handler_t handler_fn)
{
    csh handle;
    if ((cs_open(CS_ARCH_X86, CS_MODE_64, &handle) != CS_ERR_OK))
    {
        SPDLOG_ERROR("failed to init capstone");
    }

    const auto insn = cs_malloc(handle);
    size_t size = 500;
    uint64_t address = 0;
    auto fn = reinterpret_cast<const uint8_t*>(handler_fn);

    uint8_t* mulss[2] = {nullptr};
    int i = 0;

    while (cs_disasm_iter(handle, &fn, &size, &address, insn))
    {
        if (insn->id == X86_INS_MULSS)
        {
            mulss[i] = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(handler_fn) + insn->address);
            i++;
        }
    }

    cs_close(&handle);

    const auto mulss1 = mulss[0];
    const auto mulss2 = mulss[1];

    if (!mulss1 || !mulss2)
    {
        SPDLOG_ERROR("can't find scroll instructions to patch");
        return false;
    }

    DWORD old_prot;
    if (!VirtualProtect(mulss1, 8, PAGE_EXECUTE_READWRITE, &old_prot))
    {
        SPDLOG_ERROR("VirtualProtect failed");
        return false;
    }

    memset(mulss1, 0x90, 8);

    if (!VirtualProtect(mulss1, 8, old_prot, &old_prot))
    {
        SPDLOG_ERROR("VirtualProtect failed");
        return false;
    }

    if (!VirtualProtect(mulss2, 8, PAGE_EXECUTE_READWRITE, &old_prot))
    {
        SPDLOG_ERROR("VirtualProtect failed");
        return false;
    }

    memset(mulss2, 0x90, 8);

    if (!VirtualProtect(mulss1, 8, old_prot, &old_prot))
    {
        SPDLOG_ERROR("VirtualProtect failed");
        return false;
    }

    return true;
}

bool apply_scroll_patch32(flavor_id flavor_id, uint32_t scroll_value)
{
    signature_t sig_fmul1;
    signature_t sig_mulss2;

    if (flavor_id == FLAVOR_BANANA || flavor_id == FLAVOR_POTATO)
    {
        sig_fmul1 = {{0xDC, 0x0D, 0x0, 0x0, 0x0, 0x0, 0x8D, 0x0, 0x0, 0x0, 0xDE, 0xE9}, "xx????x???xx"};
        sig_mulss2 = {{0xDC, 0x0D, 0x0, 0x0, 0x0, 0x0, 0x8D, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xDE, 0xE9}, "xx????x??????xx"};
    }
    else if (flavor_id == FLAVOR_DEFAULT)
    {
        sig_fmul1 = {{0xDC, 0x0D, 0x0, 0x0, 0x0, 0x0, 0x8D, 0x0, 0x0, 0x0, 0xDE, 0xE9}, "xx????x???xx"};
        sig_mulss2 = {{0xDC, 0x0D, 0x0, 0x0, 0x0, 0x0, 0xDE, 0xE9, 0xD9}, "xx????xxx"};
    }

    const auto fmul1 = find_function_signature(sig_fmul1);
    const auto fmul2 = find_function_signature(sig_mulss2);

    if (!fmul1 || !fmul2)
    {
        SPDLOG_ERROR("can't find scroll instructions to patch");
        return false;
    }

    const auto p_fmul1_operand = static_cast<uint8_t*>(*fmul1) + 2;
    const auto p_fmul2_operand = static_cast<uint8_t*>(*fmul2) + 2;

    static double value = scroll_value;

    DWORD old_prot;
    if (!VirtualProtect(p_fmul1_operand, 4, PAGE_EXECUTE_READWRITE, &old_prot))
    {
        SPDLOG_ERROR("VirtualProtect failed");
        return false;
    }

#pragma warning(push)
#pragma warning(disable : 4311)
    *reinterpret_cast<uint32_t*>(p_fmul1_operand) = reinterpret_cast<uint32_t>(&value);
#pragma warning(pop)

    if (!VirtualProtect(p_fmul1_operand, 4, old_prot, &old_prot))
    {
        SPDLOG_ERROR("VirtualProtect failed");
        return false;
    }

    if (!VirtualProtect(p_fmul2_operand, 4, PAGE_EXECUTE_READWRITE, &old_prot))
    {
        SPDLOG_ERROR("VirtualProtect failed");
        return false;
    }

#pragma warning(push)
#pragma warning(disable : 4311)
    *reinterpret_cast<uint32_t*>(p_fmul2_operand) = reinterpret_cast<uint32_t>(&value);
#pragma warning(pop)

    if (!VirtualProtect(p_fmul2_operand, 4, old_prot, &old_prot))
    {
        SPDLOG_ERROR("VirtualProtect failed");
        return false;
    }


    return true;
}

/**
 * Uses Detours to patch a single function for late hooking
 * @param o_fn The original function
 * @param hk_fn The hook function
 * @return True if hooking was successful
 */
bool hook_single_fn(PVOID* o_fn, PVOID hk_fn)
{
    if (DetourTransactionBegin() != NO_ERROR)
    {
        SPDLOG_ERROR("DetourTransactionBegin failed");
        return false;
    }

    if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR)
    {
        SPDLOG_ERROR("DetourUpdateThread failed");
        return false;
    }

    if (DetourAttach(o_fn, hk_fn) != NO_ERROR)
    {
        SPDLOG_ERROR("DetourAttach failed");
        return false;
    }

    if (DetourTransactionCommit() != NO_ERROR)
    {
        SPDLOG_ERROR("DetourTransactionCommit failed");
        return false;
    }

    return true;
}
}
