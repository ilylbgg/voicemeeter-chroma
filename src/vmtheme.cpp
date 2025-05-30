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

#include "utils.h"

//******************//
//      WINAPI      //
//******************//

static HANDLE (WINAPI *o_CreateMutexA)(LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCSTR lpName) = CreateMutexA;
static HFONT (WINAPI *o_CreateFontIndirectA)(const LOGFONTA* lplf) = CreateFontIndirectA;
static BOOL (WINAPI *o_AppendMenuA)(HMENU hMenu, UINT uFlags, UINT_PTR uIDNewItem, LPCSTR lpNewItem) = AppendMenuA;
static HPEN (WINAPI *o_CreatePen)(int iStyle, int cWidth, COLORREF color) = CreatePen;
static HBRUSH (WINAPI *o_CreateBrushIndirect)(const LOGBRUSH* plbrush) = CreateBrushIndirect;
static COLORREF (WINAPI *o_SetTextColor)(HDC hdc, COLORREF color) = SetTextColor;
static ATOM (WINAPI *o_RegisterClassA)(const WNDCLASSA* lpWndClass) = RegisterClassA;
static BOOL (WINAPI *o_Rectangle)(HDC hdc, int left, int top, int right, int bottom) = Rectangle;

//******************//
//      CUSTOM      //
//******************//

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

static const flavor_info_t flavor_info_default = {"default", FLAVOR_DEFAULT, 0x1D1036, 0xAD70E, 0xE1036};
static const flavor_info_t flavor_info_banana = {"banana", FLAVOR_BANANA, 0x1D1036, 0x1266FE, 0xAFCB6};
static const flavor_info_t flavor_info_potato = {"potato", FLAVOR_POTATO, 0x39FEC6, 0x1ACA06, 0xE6DF6};

static std::unordered_map<flavor_id, flavor_info_t> flavor_map =
{
    {FLAVOR_DEFAULT, flavor_info_default},
    {FLAVOR_BANANA, flavor_info_banana},
    {FLAVOR_POTATO, flavor_info_potato},
};

static std::unordered_map<long, long> font_height_map = {
    {20, 18}, // input custom label
    {16, 15} // master section fader
};

static flavor_info_t active_flavor;
static std::vector<uint8_t> bg_main_bitmap_data;
static std::vector<uint8_t> bg_settings_bitmap_data;
static std::vector<uint8_t> bg_cassette_bitmap_data;
static bool init_entered = false;
static YAML::Node yaml_colors;
static o_swap_bg_t o_swap_bg = nullptr;
static o_WndProc_t o_WndProc = nullptr;

static constexpr std::wstring_view BM_FILE_BG = L"bg.bmp";
static constexpr std::wstring_view BM_FILE_BG_SETTINGS = L"bg_settings.bmp";
static constexpr std::wstring_view BM_FILE_BG_CASSETTE = L"bg_cassette.bmp";
static constexpr std::wstring_view CONFIG_FILE_THEME = L"theme.yaml";
static constexpr std::wstring_view CONFIG_FILE_COLORS = L"colors.yaml";
static constexpr std::string_view VM_MAINWINDOW_CLASSNAME = "VBCABLE0Voicemeeter0MainWindow0";

bool apply_hooks();

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
    if (!init_entered)
    {
        init_entered = true;
        utils::setup_logging();
        spdlog::info("");
        spdlog::info("vmtheme init start");
        std::optional<flavor_id> flavor_id = utils::get_flavor_id();

        if (!flavor_id)
        {
            spdlog::error("can't get Voicemeeter flavor from version info");
            return o_CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
        }

        active_flavor = flavor_map.find(*flavor_id)->second;

        auto userprofile_path = utils::get_userprofile_path();

        if (!userprofile_path)
        {
            spdlog::error("can't get userprofile path");
            return o_CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
        }

        spdlog::info("userprofile path is: {}", *utils::wstr_to_str(*userprofile_path));

        if (!std::filesystem::exists(std::filesystem::path(*userprofile_path) / CONFIG_FILE_THEME))
        {
            spdlog::error("{} not found", *utils::wstr_to_str(CONFIG_FILE_THEME.data()));
            return o_CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
        }

        std::ifstream theme_file(std::filesystem::path(*userprofile_path) / CONFIG_FILE_THEME);

        if (!theme_file.is_open())
        {
            spdlog::error("can't open {}", *utils::wstr_to_str(CONFIG_FILE_THEME.data()));
            return o_CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
        }

        YAML::Node yaml_theme;

        try
        {
            yaml_theme = YAML::Load(theme_file);
        }
        catch (YAML::ParserException& ex)
        {
            spdlog::error("failed to parse {}", *utils::wstr_to_str(CONFIG_FILE_THEME.data()));
            return o_CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
        }

        if (!yaml_theme[active_flavor.name])
        {
            spdlog::error("current Voicemeeter flavor is not found in {}", *utils::wstr_to_str(CONFIG_FILE_THEME.data()));
            return o_CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
        }

        auto active_theme_name_str = yaml_theme[active_flavor.name].as<std::string>();

        if (active_theme_name_str.empty())
        {
            spdlog::error("no theme specified in {} for current Voicemeeter flavor", *utils::wstr_to_str(CONFIG_FILE_THEME.data()));
            return o_CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
        }

        auto active_theme_name_wstr = utils::str_to_wstr(active_theme_name_str);

        if (!active_theme_name_wstr)
        {
            spdlog::error("active_theme_name_str conversion error");
            return o_CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
        }

        auto active_flavor_name = utils::str_to_wstr(active_flavor.name);

        if (!active_flavor_name)
        {
            spdlog::error("active_flavor.name conversion error");
            return o_CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
        }

        std::wstring theme_path = (std::filesystem::path(*userprofile_path) / L"themes" / *active_theme_name_wstr / *active_flavor_name);

        if (!std::filesystem::exists(std::filesystem::path(theme_path) / BM_FILE_BG))
        {
            spdlog::error("can't find {} in themes folder", *utils::wstr_to_str(BM_FILE_BG.data()));
            return o_CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
        }

        if (!utils::load_bitmap(std::filesystem::path(theme_path) / BM_FILE_BG, bg_main_bitmap_data))
        {
            spdlog::error("error loading {}", *utils::wstr_to_str(BM_FILE_BG.data()));
            return o_CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
        }

        if (!std::filesystem::exists(std::filesystem::path(theme_path) / BM_FILE_BG_SETTINGS))
        {
            spdlog::error("can't find {} in themes folder", *utils::wstr_to_str(BM_FILE_BG_SETTINGS.data()));
            return o_CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
        }

        if (!utils::load_bitmap(std::filesystem::path(theme_path) / BM_FILE_BG_SETTINGS, bg_settings_bitmap_data))
        {
            spdlog::error("error loading {}", *utils::wstr_to_str(BM_FILE_BG_SETTINGS.data()));
            return o_CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
        }

        if (!std::filesystem::exists(std::filesystem::path(theme_path) / BM_FILE_BG_CASSETTE))
        {
            spdlog::error("can't find {} in themes folder", *utils::wstr_to_str(BM_FILE_BG_CASSETTE.data()));
            return o_CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
        }

        if (!utils::load_bitmap(std::filesystem::path(theme_path) / BM_FILE_BG_CASSETTE, bg_cassette_bitmap_data))
        {
            spdlog::error("error loading {}", *utils::wstr_to_str(BM_FILE_BG_CASSETTE.data()));
            return o_CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
        }

        if (!std::filesystem::exists(std::filesystem::path(*userprofile_path) / L"themes" / *active_theme_name_wstr / CONFIG_FILE_COLORS))
        {
            spdlog::error("can't find {}", *utils::wstr_to_str(CONFIG_FILE_COLORS.data()));
            return o_CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
        }

        std::ifstream colors_file(std::filesystem::path(*userprofile_path) / L"themes" / *active_theme_name_wstr / CONFIG_FILE_COLORS);

        if (!colors_file.is_open())
        {
            spdlog::error("can't open {}", *utils::wstr_to_str(CONFIG_FILE_COLORS.data()));
            return o_CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
        }

        try
        {
            yaml_colors = YAML::Load(colors_file);
        }
        catch (YAML::ParserException& ex)
        {
            spdlog::error("failed to parse {}", *utils::wstr_to_str(CONFIG_FILE_COLORS.data()));
            return o_CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
        }

        spdlog::info("vmtheme init success");
        spdlog::info("finish hooking static functions...");

        if (!apply_hooks())
        {
            spdlog::error("hooking failed");
            return o_CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
        }

        spdlog::info("hooking success");
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
    if (auto new_col = utils::get_yaml_color(yaml_colors, utils::colorref_to_hex(color), CATEGORY_SHAPES))
    {
        if (auto new_col_colorref = utils::hex_to_colorref(*new_col))
            color = *new_col_colorref;
    }

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
    if (std::optional<std::string> new_col = utils::get_yaml_color(yaml_colors, utils::colorref_to_hex(plbrush->lbColor), CATEGORY_SHAPES))
    {
        if (auto new_col_colorref = utils::hex_to_colorref(*new_col))
            plbrush->lbColor = *new_col_colorref;
    }

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
    if (std::optional<std::string> new_col = utils::get_yaml_color(yaml_colors, utils::colorref_to_hex(color), CATEGORY_TEXT))
    {
        if (auto new_col_colorref = utils::hex_to_colorref(*new_col))
            color = *new_col_colorref;
    }

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
        ShellExecute(nullptr, L"open", L"https://github.com/emkaix/voicemeeter-themes-mod", nullptr, nullptr, SW_SHOW);

    return o_WndProc(hWnd, Msg, wParam, lParam);
}

/**
 * We hook this function in order to get the address of WndProc from the lpWndClass pointer, so we can hook WndProc
 * See https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerclassa
 */
ATOM WINAPI hk_RegisterClassA(const WNDCLASSA* lpWndClass)
{
    if (lpWndClass->lpszClassName == VM_MAINWINDOW_CLASSNAME)
    {
        spdlog::info("hook WndProc...");
        o_WndProc = lpWndClass->lpfnWndProc;

        if (DetourTransactionBegin() != NO_ERROR)
            return false;

        if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR)
            return false;

        if (DetourAttach(&reinterpret_cast<PVOID&>(o_WndProc), hk_WndProc) != NO_ERROR)
            return false;

        if (DetourTransactionCommit() != NO_ERROR)
            return false;

        spdlog::info("hook WndProc success");
    }

    return o_RegisterClassA(lpWndClass);
}

/**
 * We hook this function to disable drawing specific rectangles that are supposed to mask the background
 * See https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-rectangle
 */
BOOL WINAPI hk_Rectangle(HDC hdc, int left, int top, int right, int bottom)
{
    if (active_flavor.id == FLAVOR_POTATO)
    {
        if ((left == 1469 && top == 15) || // box inside menu button
            (left == 1221 && top == 581) || // bus fader box
            (left == 1159 && top == 581) || // bus fader box
            (left == 1345 && top == 581) || // bus fader box
            (left == 1283 && top == 581)) // bus fader box
            return true;
    }

    if (active_flavor.id == FLAVOR_BANANA)
    {
        if ((left == 848 && top == 15) || // box inside menu button
            (left == 789 && top == 432) || // bus fader box
            (left == 727 && top == 432) || // bus fader box
            (left == 913 && top == 432) || // bus fader box
            (left == 851 && top == 432)) // bus fader box
            return true;
    }

    return o_Rectangle(hdc, left, top, right, bottom);
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
    if (size == active_flavor.bitmap_size_main)
        return o_swap_bg(bg_main_bitmap_data.data(), size);

    if (size == active_flavor.bitmap_size_settings)
        return o_swap_bg(bg_settings_bitmap_data.data(), size);

    if (size == active_flavor.bitmap_size_cassette)
        return o_swap_bg(bg_cassette_bitmap_data.data(), size);

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
    auto bitmap_main_header = reinterpret_cast<LPBITMAPFILEHEADER>(bg_main_bitmap_data.data());
    auto bitmap_settings_header = reinterpret_cast<LPBITMAPFILEHEADER>(bg_settings_bitmap_data.data());
    uint32_t bitmap_main_data_size = active_flavor.bitmap_size_main - bitmap_main_header->bfOffBits;
    uint32_t bitmap_settings_data_size = active_flavor.bitmap_size_settings - bitmap_settings_header->bfOffBits;

    if (size == bitmap_main_data_size)
        return o_swap_bg(ppvBits, &bg_main_bitmap_data[bitmap_main_header->bfOffBits], bitmap_main_data_size);

    if (size == bitmap_settings_data_size)
        return o_swap_bg(ppvBits, &bg_settings_bitmap_data[bitmap_settings_header->bfOffBits], bitmap_settings_data_size);

    return o_swap_bg(ppvBits, data_ptr, size);
}
#endif

//*****************************//
//        DETOURS SETUP        //
//*****************************//

static std::vector<std::pair<PVOID*, PVOID>> hooks = {
    {&reinterpret_cast<PVOID&>(o_CreateFontIndirectA), hk_CreateFontIndirectA},
    {&reinterpret_cast<PVOID&>(o_AppendMenuA), hk_AppendMenuA},
    {&reinterpret_cast<PVOID&>(o_CreatePen), hk_CreatePen},
    {&reinterpret_cast<PVOID&>(o_CreateBrushIndirect), hk_CreateBrushIndirect},
    {&reinterpret_cast<PVOID&>(o_SetTextColor), hk_SetTextColor},
    {&reinterpret_cast<PVOID&>(o_RegisterClassA), hk_RegisterClassA},
    {&reinterpret_cast<PVOID&>(o_Rectangle), hk_Rectangle},
    {&reinterpret_cast<PVOID&>(o_swap_bg), hk_swap_bg},
    {&reinterpret_cast<PVOID&>(o_WndProc), hk_WndProc}
};

/**
 * Initializes Detours hooks
 * @return True if hooks are attached successfully, false otherwise
 */
bool apply_hooks()
{
    auto o_swap_bg_optional = utils::find_function_signature(sig_swap_bg);

    if (!o_swap_bg_optional)
    {
        spdlog::error("unable to find swap bg function");
        return false;
    }

    o_swap_bg = reinterpret_cast<o_swap_bg_t>(*o_swap_bg_optional);

    if (DetourTransactionBegin() != NO_ERROR)
        return false;

    if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR)
        return false;

    for (const auto& [original, hook] : hooks)
    {
        if (*original != nullptr && DetourAttach(original, hook) != NO_ERROR)
        {
            spdlog::error("unable to hook functions");
            return false;
        }
    }

    if (DetourTransactionCommit() != NO_ERROR)
        return false;

    return true;
}

/**
 * Initializes only the CreateMutexA hook, where other functions are hooked and vmtheme is initialized properly.
 * @return True if hooks are attached successfully, false otherwise
 */
bool apply_initial_hook()
{
    if (DetourTransactionBegin() != NO_ERROR)
        return false;

    if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR)
        return false;

    if (DetourAttach(&reinterpret_cast<PVOID&>(o_CreateMutexA), hk_CreateMutexA) != NO_ERROR)
        return false;

    if (DetourTransactionCommit() != NO_ERROR)
        return false;

    return true;
}

/**
 * Detours needs a single exported function with ordinal 1
 */
void dummy_export()
{
}

/**
 * DLL entry point
 * Contains only code to initialize and clean up Detours
 */
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        utils::attach_console_debug();
        return apply_initial_hook();
    }

    return TRUE;
}
