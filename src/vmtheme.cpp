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
#include <detours.h>
#include <fstream>
#include <psapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <sstream>
#include <nlohmann/json.hpp>
#include "shared.h"

using json = nlohmann::json;

//******************//
//      WINAPI      //
//******************//

static HANDLE (WINAPI *o_CreateMutexA)(LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCSTR lpName) = CreateMutexA;
static HFONT (WINAPI *o_CreateFontIndirectA)(const LOGFONTA* lplf) = CreateFontIndirectA;
static BOOL (WINAPI *o_AppendMenuA)(HMENU hMenu, UINT uFlags, UINT_PTR uIDNewItem, LPCSTR lpNewItem) = AppendMenuA;
static HPEN (WINAPI *o_CreatePen)(int iStyle, int cWidth, COLORREF color) = CreatePen;
static HBRUSH (WINAPI *o_CreateBrushIndirect)(const LOGBRUSH* plbrush) = CreateBrushIndirect;
static COLORREF (WINAPI *o_SetTextColor)(HDC hdc, COLORREF color) = SetTextColor;
static ATOM (WINAPI *o_RegisterClassA)(const WNDCLASSA *lpWndClass) = RegisterClassA;

//******************//
//      CUSTOM      //
//******************//

typedef struct signature
{
    const std::vector<uint8_t> pattern;
    const char* mask;
} signature_t;

// signatures are stable between VM flavors and only differ for architecture (32/64bit)
#if defined(_WIN64)
static signature_t sig_swap_bg = {{0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x20, 0x56, 0x48, 0x83}, {"xxxxxxxxxx"}};
typedef HBITMAP (__fastcall *o_swap_bg_t)(uint8_t* data_ptr, uint32_t res_size);
typedef LRESULT (__fastcall *o_WndProc_t)(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
#else
static signature_t sig_swap_bg = {{0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x53, 0x56, 0x57, 0x3B, 0x45, 0x0C}, {"xxxxxxxxxxxx"}};
typedef void (__cdecl *o_swap_bg_t)(uint8_t** ppvBits, uint8_t* data_ptr, uint32_t size);
typedef LRESULT (__stdcall *o_WndProc_t)(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
#endif

//******************//
//      GLOBALS     //
//******************//

typedef struct flavor_info
{
    std::string specifier;
    uint32_t bitmap_size{};
} flavor_info_t;

static const flavor_info_t flavor_info_default = {"default", 0x1D1036};
static const flavor_info_t flavor_info_banana = {"banana", 0x1D1036};
static const flavor_info_t flavor_info_potato = {"potato", 0x39FEC6};

static std::unordered_map<std::wstring, flavor_info_t> flavor_map =
{
    {L"voicemeeter_x64.exe", flavor_info_default},
    {L"voicemeeter.exe", flavor_info_default},
    {L"voicemeeterpro_x64.exe", flavor_info_banana},
    {L"voicemeeterpro.exe", flavor_info_banana},
    {L"voicemeeter8x64.exe", flavor_info_potato},
    {L"voicemeeter8.exe", flavor_info_potato}
};

static std::unordered_map<long, long> font_height_map = {
    {20, 18}, // input custom label
    {16, 15}  // master section fader
};

static flavor_info_t active_flavor;
static std::vector<uint8_t> bg_bitmap_data;
static bool init_complete = false;
static json json_colors;
static o_swap_bg_t o_swap_bg = nullptr;
static o_WndProc_t o_WndProc = nullptr;
static std::wstring userprofile_path;

//******************//
// HELPER FUNCTIONS //
//******************//

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
COLORREF hex_to_colorref(const std::string& hex)
{
    if (hex.empty())
        error(L"error empty color value");

    std::string clean_hex = (hex[0] == '#') ? hex.substr(1) : hex;

    if (clean_hex.length() != 6)
        error(L"invalid color value");

    unsigned long value = 0;

    try
    {
        value = std::stoul(clean_hex, nullptr, 16);
    }
    catch (...)
    {
        error(L"invalid color value");
    }

    uint8_t r = (value >> 16) & 0xFF;
    uint8_t g = (value >> 8) & 0xFF;
    uint8_t b = value & 0xFF;

    return RGB(r, g, b);
}

/**
 * Find non-exported functions using signature scanning
 * Function signatures should be stable across updates
 * Simple O(n*m) implementation
 * @param sig A signature struct containing the byte pattern and a mask
 * @return The absolute address of the function
 */
PVOID find_function_signature(const signature_t& sig)
{
    HMODULE h_module = GetModuleHandle(nullptr);
    MODULEINFO mod_info;

    if (!h_module)
        error(L"error GetModuleHandle");

    if (!GetModuleInformation(GetCurrentProcess(), h_module, &mod_info, sizeof(mod_info)))
        error(L"error GetModuleInformation");

    uint8_t* start = static_cast<uint8_t*>(mod_info.lpBaseOfDll);
    size_t end = mod_info.SizeOfImage;
    size_t pattern_size = sig.pattern.size();
    const uint8_t* pattern = sig.pattern.data();
    const char* mask = sig.mask;

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

    return nullptr;
}

/**
 * Loads the bitmap file from userprofile folder "C:\Users\<User>\Documents\Voicemeeter"
 * @param path Path to bitmap
 */
void load_bitmap(const std::wstring& path)
{
    std::ifstream f(path.c_str(), std::ios::binary | std::ios::ate);

    if (!f.is_open())
        error(L"error opening bitmap");

    std::streampos size = f.tellg();
    f.seekg(0, std::ios::beg);
    bg_bitmap_data.assign(size, '\0');

    if (!f.read(reinterpret_cast<char*>(bg_bitmap_data.data()), size))
        error(L"error reading bitmap");
}

/**
 * Gets the path to the Voicemeeter user directory
 * @return Path to VM directory
 */
std::wstring get_userprofile_path()
{
    PWSTR buffer = nullptr;

    if (SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &buffer) != S_OK)
        error(L"SHGetKnownFolderPath");

    std::wstring userprofile_path = buffer;
    userprofile_path += L"\\Documents\\Voicemeeter";
    CoTaskMemFree(buffer);

    return userprofile_path;
}

/**
 * Gets a color value from the json in a case-insensitive way
 * @param arg_col The color value in upper case
 * @param category Can either be "shapes" or "text"
 * @return The mapped color value for the current theme
 */
std::optional<std::string> get_json_color(const std::string& arg_col, const std::string& category)
{
    for (auto& [k, v] : json_colors[category.c_str()].items())
    {
        std::string temp(k.size(), '\0');
        std::transform(k.begin(), k.end(), temp.begin(), ::toupper);

        if (temp == arg_col)
        {
            std::string res = v["value"].get<std::string>();

            if (!res.empty())
                return res;

            return std::nullopt;
        }
    }

    return std::nullopt;
}

//*****************************//
//      HOOKED FUNCTIONS       //
//*****************************//

/**
 * We hook this function to initialize the theme, because it gets called early in WinMain and is exported
 * The theme config is loaded from "C:\Users\<User>\Documents\Voicemeeter"
 * See https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createmutexa
 */
HANDLE WINAPI hk_CreateMutexA(LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCSTR lpName)
{
    if (!init_complete)
    {
        std::wstring executable_name(MAX_PATH, '\0');

        if (!GetModuleFileName(nullptr, executable_name.data(), MAX_PATH))
            error(L"error GetModuleFileName");

        PathStripPath(const_cast<LPWSTR>(executable_name.c_str()));

        active_flavor = flavor_map[executable_name.c_str()];

        if (active_flavor.specifier.empty())
            error(L"error flavor_map");

        userprofile_path = get_userprofile_path();
        std::ifstream theme_file(userprofile_path + L"\\theme.json");

        if (!theme_file.is_open())
            error(L"error opening theme.json");

        json json_theme = json::parse(theme_file);
        std::string active_theme_name_str = json_theme[active_flavor.specifier].get<std::string>();

        if (active_theme_name_str.empty())
            error(L"error no theme in json");

        std::wstring active_theme_name_wstr = str_to_wstr(active_theme_name_str);
        std::wstring theme_path = userprofile_path + L"\\themes\\" + str_to_wstr(active_flavor.specifier) + L"\\" + active_theme_name_wstr;
        load_bitmap(theme_path + L"\\bg.bmp");
        std::ifstream color_file(theme_path + L"\\colors.json");

        if (!color_file.is_open())
            error(L"error opening colors.json");

        json_colors = json::parse(color_file);
        init_complete = true;
    }

    return o_CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
}

/**
 * Creates a font object
 * We hook this function to change the font
 * See https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createfontindirecta
 */
HFONT WINAPI hk_CreateFontIndirectA(const LOGFONTA* lplf)
{
    LOGFONTA modified_log_font = *lplf;
    long new_size = font_height_map[lplf->lfHeight];
    modified_log_font.lfHeight = new_size != 0 ? new_size : lplf->lfHeight;
    modified_log_font.lfQuality = CLEARTYPE_NATURAL_QUALITY;

    return o_CreateFontIndirectA(&modified_log_font);
}

/**
 * Adds a menu item to a menu
 * We hook this function to add custom menu items to the main menu
 * See https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-appendmenua
 */
BOOL WINAPI hk_AppendMenuA(HMENU hMenu, UINT uFlags, UINT_PTR uIDNewItem, LPCSTR lpNewItem)
{
    if (uIDNewItem == 0x1F9u)
    {
        o_AppendMenuA(hMenu, uFlags, uIDNewItem, lpNewItem);

        return o_AppendMenuA(hMenu, uFlags, 0x1337, VMTHEME_VERSION);
    }

    return o_AppendMenuA(hMenu, uFlags, uIDNewItem, lpNewItem);
}

/**
 * GDI function used to draw lines
 * We hook this function to change the color of UI elements made up of lines
 * Color values are parsed from the colors.json
 * See https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createpen
 */
HPEN WINAPI hk_CreatePen(int iStyle, int cWidth, COLORREF color)
{
    std::optional<std::string> new_col = get_json_color(colorref_to_hex(color), "shapes");

    if (new_col)
        color = hex_to_colorref(*new_col);

    return o_CreatePen(iStyle, cWidth, color);
}

/**
 * GDI function used to draw forms like filled rectangles
 * We hook this function to change the color of UI elements made up of such forms
 * Color values are parsed from the colors.json
 * See https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createbrushindirect
 */
HBRUSH WINAPI hk_CreateBrushIndirect(LOGBRUSH* plbrush)
{
    std::optional<std::string> new_col = get_json_color(colorref_to_hex(plbrush->lbColor), "shapes");

    if (new_col)
        plbrush->lbColor = hex_to_colorref(*new_col);

    return o_CreateBrushIndirect(plbrush);
}

/**
 * GDI function used to set the color of text
 * We hook this function to change text color
 * Color values are parsed from the colors.json
 * See https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-settextcolor
 */
COLORREF WINAPI hk_SetTextColor(HDC hdc, COLORREF color)
{
    std::optional<std::string> new_col = get_json_color(colorref_to_hex(color), "text");

    if (new_col)
        color = hex_to_colorref(*new_col);

    return o_SetTextColor(hdc, color);
}


#if defined (_WIN64)
/**
 * We hook WndProc in order to listen for a click on the vmtheme menu item, which opens the Github URL in the browser
 * See https://learn.microsoft.com/en-us/windows/win32/api/winuser/nc-winuser-wndproc
 */
LRESULT __fastcall hk_WndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
#else
/**
 * We hook WndProc in order to listen for a click on the vmtheme menu item, which opens the Github URL in the browser
 * See https://learn.microsoft.com/en-us/windows/win32/api/winuser/nc-winuser-wndproc
 */
LRESULT __stdcall hk_WndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
#endif
{
    if (Msg == WM_COMMAND && LOWORD(wParam) == 0x1337)
        ShellExecute(nullptr, L"open", L"https://github.com/emkaix/voicemeeter-themes", nullptr, nullptr, SW_SHOW);

    return o_WndProc(hWnd, Msg, wParam, lParam);
}

/**
 * We hook this function in order to get the address of WndProc from the lpWndClass pointer, so we can hook WndProc
 * See https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerclassa
 */
ATOM WINAPI hk_RegisterClassA(const WNDCLASSA *lpWndClass)
{
    if (std::strcmp(lpWndClass->lpszClassName, "VBCABLE0Voicemeeter0MainWindow0") == 0)
    {
        o_WndProc = lpWndClass->lpfnWndProc;

        if (DetourTransactionBegin() != NO_ERROR)
            error(L"error DetourTransactionBegin");

        if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR)
            error(L"error DetourUpdateThread");

        if (DetourAttach(&reinterpret_cast<PVOID&>(o_WndProc), hk_WndProc) != NO_ERROR)
            error(L"error DetourAttach");

        if (DetourTransactionCommit() != NO_ERROR)
            error(L"error DetourTransactionCommit");
    }

    return o_RegisterClassA(lpWndClass);
}


#if defined(_WIN64)
/**
 * Copies the decrypted bitmap data into a buffer created by CreateDIBSection
 * This function is hooked for the 64bit versions of VM
 * Here we simply swap out the default bitmap with our bitmap
 * @param data_ptr Pointer to the beginning of the bitmap header
 * @param size Size of the Bitmap file including the header
 * @return Not used
 */
HBITMAP __fastcall hk_swap_bg(uint8_t* data_ptr, uint32_t size)
{
    if (size == active_flavor.bitmap_size)
        return o_swap_bg(bg_bitmap_data.data(), size);

    return o_swap_bg(data_ptr, size);
}
#else
/**
 * Copies the decrypted bitmap data into a buffer created by CreateDIBSection
 * This function is hooked for the 32bit versions of VM because the other one has a weird calling convention
 * Here we simply swap out the default bitmap with our bitmap
 * @param ppvBits Not used
 * @param data_ptr Pointer to the actual data section of the bitmap, skipping the header
 * @param size Size of the actual bitmap data, excluding the header size
 */
void __cdecl hk_swap_bg(uint8_t** ppvBits, uint8_t* data_ptr, uint32_t size)
{
    LPBITMAPFILEHEADER bitmap_header = reinterpret_cast<LPBITMAPFILEHEADER>(bg_bitmap_data.data());
    uint32_t bitmap_data_size = active_flavor.bitmap_size - bitmap_header->bfOffBits;

    if (size == bitmap_data_size)
        return o_swap_bg(ppvBits, &bg_bitmap_data[bitmap_header->bfOffBits], bitmap_data_size);

    return o_swap_bg(ppvBits, data_ptr, size);
}
#endif

//*****************************//
//        DETOURS SETUP        //
//*****************************//

/**
 * Initializes Detours hooks
 */
void init_hooks()
{
    o_swap_bg = reinterpret_cast<o_swap_bg_t>(find_function_signature(sig_swap_bg));

    if (!o_swap_bg)
        error(L"error find_function_signature");

    if (!(DetourRestoreAfterWith()))
        error(L"DetourRestoreAfterWith");

    if (DetourTransactionBegin() != NO_ERROR)
        error(L"error DetourTransactionBegin");

    if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR)
        error(L"error DetourUpdateThread");

    if (DetourAttach(&reinterpret_cast<PVOID&>(o_CreateMutexA), hk_CreateMutexA) != NO_ERROR)
        error(L"error DetourAttach");

    if (DetourAttach(&reinterpret_cast<PVOID&>(o_CreateFontIndirectA), hk_CreateFontIndirectA) != NO_ERROR)
        error(L"error DetourAttach");

    if (DetourAttach(&reinterpret_cast<PVOID&>(o_AppendMenuA), hk_AppendMenuA) != NO_ERROR)
        error(L"error DetourAttach");

    if (DetourAttach(&reinterpret_cast<PVOID&>(o_CreatePen), hk_CreatePen) != NO_ERROR)
        error(L"error DetourAttach");

    if (DetourAttach(&reinterpret_cast<PVOID&>(o_CreateBrushIndirect), hk_CreateBrushIndirect) != NO_ERROR)
        error(L"error DetourAttach");

    if (DetourAttach(&reinterpret_cast<PVOID&>(o_SetTextColor), hk_SetTextColor))
        error(L"error DetourAttach");

    if (DetourAttach(&reinterpret_cast<PVOID&>(o_RegisterClassA), hk_RegisterClassA))
        error(L"error DetourAttach");

    if (DetourAttach(&reinterpret_cast<PVOID&>(o_swap_bg), hk_swap_bg) != NO_ERROR)
        error(L"error DetourAttach");

    if (DetourTransactionCommit() != NO_ERROR)
        error(L"error DetourTransactionCommit");
}

/**
 * Clean up Detours hooks
 */
void cleanup_hooks()
{
    if (DetourTransactionBegin() != NO_ERROR)
        error(L"error DetourTransactionBegin");

    if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR)
        error(L"error DetourUpdateThread");

    if (DetourDetach(&reinterpret_cast<PVOID&>(o_CreateMutexA), hk_CreateMutexA) != NO_ERROR)
        error(L"error DetourDetach");

    if (DetourDetach(&reinterpret_cast<PVOID&>(o_CreateFontIndirectA), hk_CreateFontIndirectA) != NO_ERROR)
        error(L"error DetourDetach");

    if (DetourDetach(&reinterpret_cast<PVOID&>(o_AppendMenuA), hk_AppendMenuA) != NO_ERROR)
        error(L"error DetourDetach");

    if (DetourDetach(&reinterpret_cast<PVOID&>(o_CreatePen), hk_CreatePen) != NO_ERROR)
        error(L"error DetourDetach");

    if (DetourDetach(&reinterpret_cast<PVOID&>(o_CreateBrushIndirect), hk_CreateBrushIndirect) != NO_ERROR)
        error(L"error DetourDetach");

    if (DetourDetach(&reinterpret_cast<PVOID&>(o_SetTextColor), hk_SetTextColor))
        error(L"error DetourDetach");

    if (DetourDetach(&reinterpret_cast<PVOID&>(o_RegisterClassA), hk_RegisterClassA))
        error(L"error DetourDetach");

    if (o_WndProc != nullptr && DetourDetach(&reinterpret_cast<PVOID&>(o_WndProc), hk_WndProc) != NO_ERROR)
        error(L"error DetourDetach");

    if (o_swap_bg != nullptr && DetourDetach(&reinterpret_cast<PVOID&>(o_swap_bg), hk_swap_bg) != NO_ERROR)
        error(L"error DetourDetach");

    if (DetourTransactionCommit() != NO_ERROR)
        error(L"error DetourTransactionCommit");
}

/**
 * DLL entry point
 * Contains only code to initialize and clean up Detours
 */
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (DetourIsHelperProcess())
        return TRUE;

    if (fdwReason == DLL_PROCESS_ATTACH)
    {
#ifndef NDEBUG
        attach_console();
#endif
        init_hooks();
    }
    else if (fdwReason == DLL_PROCESS_DETACH)
    {
        cleanup_hooks();
    }

    return TRUE;
}
