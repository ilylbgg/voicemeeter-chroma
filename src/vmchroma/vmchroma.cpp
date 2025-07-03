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
#include <detours.h>
#include <string>
#include <optional>
#include <vector>
#include <shlwapi.h>
#include <filesystem>
#include <shlobj.h>
#include <wingdi.h>
#include <d2d1_1.h>

#include "utils.hpp"
#include "winapi_hook_defs.hpp"
#include "window_manager.hpp"
#include "config_manager.hpp"

//******************//
//      WINAPI      //
//******************//

HANDLE (WINAPI *o_CreateMutexA)(LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCSTR lpName) = CreateMutexA;
HFONT (WINAPI *o_CreateFontIndirectA)(const LOGFONTA* lplf) = CreateFontIndirectA;
BOOL (WINAPI *o_AppendMenuA)(HMENU hMenu, UINT uFlags, UINT_PTR uIDNewItem, LPCSTR lpNewItem) = AppendMenuA;
HPEN (WINAPI *o_CreatePen)(int iStyle, int cWidth, COLORREF color) = CreatePen;
HBRUSH (WINAPI *o_CreateBrushIndirect)(const LOGBRUSH* plbrush) = CreateBrushIndirect;
COLORREF (WINAPI *o_SetTextColor)(HDC hdc, COLORREF color) = SetTextColor;
ATOM (WINAPI *o_RegisterClassA)(const WNDCLASSA* lpWndClass) = RegisterClassA;
BOOL (WINAPI *o_Rectangle)(HDC hdc, int left, int top, int right, int bottom) = Rectangle;
HBITMAP (WINAPI *o_CreateDIBSection)(HDC hdc, const BITMAPINFO* pbmi, UINT usage, void** ppvBits, HANDLE hSection, DWORD offset) = CreateDIBSection;
HDC (WINAPI *o_BeginPaint)(HWND hWnd, LPPAINTSTRUCT lpPaint) = BeginPaint;
UINT_PTR (WINAPI *o_SetTimer)(HWND hWnd, UINT_PTR nIDEvent, UINT uElapse, TIMERPROC lpTimerFunc) = SetTimer;
HDC (WINAPI *o_GetDC)(HWND hWnd) = GetDC;
int (WINAPI *o_ReleaseDC)(HWND hWnd, HDC hDC) = ReleaseDC;
BOOL (WINAPI *o_SetWindowPos)(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags) = SetWindowPos;
BOOL (WINAPI *o_TrackPopupMenu)(HMENU hMenu, UINT uFlags, int x, int y, int nReserved, HWND hWnd, const RECT* prcRect) = TrackPopupMenu;
BOOL (WINAPI *o_GetClientRect)(HWND hWnd, LPRECT lpRect) = GetClientRect;
HWND (WINAPI *o_CreateWindowExA)(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam) = CreateWindowExA;
INT_PTR (WINAPI *o_DialogBoxIndirectParamA)(HINSTANCE hInstance, LPCDLGTEMPLATEA hDialogTemplate, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam) = DialogBoxIndirectParamA;

static signature_t sig_handle_scroll = {{0x48, 0x89, 0x74, 0x24, 0x20, 0x41, 0x54, 0x48, 0x83, 0xEC, 0x00, 0x83, 0xB9}, {"xxxxxxxxxx?xx"}};

//******************//
//      GLOBALS     //
//******************//

std::unique_ptr<window_manager> wm;
std::unique_ptr<config_manager> cm;

static std::unordered_map<long, long> font_height_map = {
    {20, 18}, // input custom label
    {16, 15} // master section fader
};

static bool init_entered = false;
static o_scroll_handler_t o_scroll_handler = nullptr;
static WNDPROC o_WndProc_main = nullptr;
static o_WndProc_chldwnd_t o_WndProc_comp = nullptr;
static o_WndProc_chldwnd_t o_WndProc_denoiser = nullptr;
static o_WndProc_chldwnd_t o_WndProc_wdb = nullptr;
static HMENU tray_menu = nullptr;

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

        wm = std::make_unique<window_manager>();
        cm = std::make_unique<config_manager>();

        if (!cm->load_config())
        {
            SPDLOG_ERROR("failed to load config");
            utils::mbox_error(L"failed to load config, check error log for more details");
            return o_CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
        }

        if (!cm->init_theme())
        {
            SPDLOG_ERROR("failed to init theme");
            utils::mbox_error(L"failed to init theme, check error log for more details");
            return o_CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
        }

        if (!apply_hooks())
        {
            SPDLOG_ERROR("hooking failed");
            return o_CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
        }
    }

    return o_CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
}

/**
 * Creates a font object
 * We hook this function to change the font size and quality
 * See https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createfontindirecta
 */
HFONT WINAPI hk_CreateFontIndirectA(const LOGFONTA* lplf)
{
    LOGFONTA modified_log_font = *lplf;
    const long new_size = font_height_map[lplf->lfHeight];
    modified_log_font.lfHeight = new_size != 0 ? new_size : lplf->lfHeight;
    modified_log_font.lfQuality = *cm->cfg_get_font_quality();

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

        return o_AppendMenuA(hMenu, uFlags, 0x1337, VMCHROMA_VERSION);
    }

    // get tray menu handle
    if (lpNewItem != nullptr && strcmp(lpNewItem, "Exit Menu") == 0)
        tray_menu = hMenu;

    return o_AppendMenuA(hMenu, uFlags, uIDNewItem, lpNewItem);
}

/**
 * GDI function used to draw lines
 * We hook this function to change the color of UI elements made up of lines
 * Color values are parsed from the colors.yaml
 * See https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createpen
 */
HPEN WINAPI hk_CreatePen(int iStyle, int cWidth, COLORREF color)
{
    if (const auto new_col_opt = cm->cfg_get_color(utils::colorref_to_hex(color), CATEGORY_SHAPES))
    {
        if (const auto new_col = utils::hex_to_colorref(*new_col_opt))
            color = *new_col;
    }

    return o_CreatePen(iStyle, cWidth, color);
}

/**
 * GDI function used to draw forms like filled rectangles
 * We hook this function to change the color of UI elements made up of such forms
 * Color values are parsed from the colors.yaml
 * See https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createbrushindirect
 */
HBRUSH WINAPI hk_CreateBrushIndirect(LOGBRUSH* plbrush)
{
    if (const auto new_col_opt = cm->cfg_get_color(utils::colorref_to_hex(plbrush->lbColor), CATEGORY_SHAPES))
    {
        if (const auto new_col = utils::hex_to_colorref(*new_col_opt))
            plbrush->lbColor = *new_col;
    }

    return o_CreateBrushIndirect(plbrush);
}

/**
 * GDI function used to set the color of text
 * We hook this function to change text color
 * Color values are parsed from the colors.yaml
 * See https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-settextcolor
 */
COLORREF WINAPI hk_SetTextColor(HDC hdc, COLORREF color)
{
    if (const auto new_col_opt = cm->cfg_get_color(utils::colorref_to_hex(color), CATEGORY_TEXT))
    {
        if (const auto new_col = utils::hex_to_colorref(*new_col_opt))
            color = *new_col;
    }

    return o_SetTextColor(hdc, color);
}

/**
 * Sets the time interval for WM_TIMER messages, used to dynamically update UI elements without user interaction
 * Interval value is parsed from the vmchroma.yaml
 * See https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-settimer
 */
UINT_PTR WINAPI hk_SetTimer(HWND hWnd, UINT_PTR nIDEvent, UINT uElapse, TIMERPROC lpTimerFunc)
{
    if (nIDEvent == 12346)
    {
        if (const auto interval = cm->cfg_get_ui_update_interval())
            return o_SetTimer(hWnd, nIDEvent, *interval, lpTimerFunc);
    }

    return o_SetTimer(hWnd, nIDEvent, uElapse, lpTimerFunc);
}

/**
 * We hook this function to disable drawing specific rectangles that are supposed to mask the background
 * See https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-rectangle
 */
BOOL WINAPI hk_Rectangle(HDC hdc, int left, int top, int right, int bottom)
{
    if (cm->get_current_flavor_id() == FLAVOR_POTATO)
    {
        if ((left == 1469 && top == 15) || // box inside menu button
            (left == 1221 && top == 581) || // bus fader box
            (left == 1159 && top == 581) || // bus fader box
            (left == 1345 && top == 581) || // bus fader box
            (left == 1283 && top == 581)) // bus fader box
            return true;
    }

    if (cm->get_current_flavor_id() == FLAVOR_BANANA)
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

/**
 * Handler function called on WM_MOUSEWHEEL messages
 * We hook this function in order to change the amount of dB change on the faders
 * Values are parsed from the vmchroma.yaml file
 */
void ARCH_CALL hk_scroll_handler(uint64_t* a1, HWND hwnd, uint32_t x, uint32_t y, uint32_t a5)
{
    const auto shift_val = cm->cfg_get_fader_shift_scroll_step();
    const auto normal_val = cm->cfg_get_fader_scroll_step();

    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
    {
        if (shift_val)
            a5 *= *shift_val;
    }
    else
    {
        if (normal_val)
            a5 *= *normal_val;
    }

    return o_scroll_handler(a1, hwnd, x, y, a5);
}

/**
 * Provides a pointer to a buffer for the loaded bitmaps
 * We hook this function in order to write our own background bitmaps to the buffer
 * See https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createdibsection
 */
HBITMAP WINAPI hk_CreateDIBSection(HDC hdc, BITMAPINFO* pbmi, UINT usage, void** ppvBits, HANDLE hSection, DWORD offset)
{
    void* ppvBits_new = nullptr;
    const uint8_t* bm_data = nullptr;

    if (pbmi->bmiHeader.biWidth == cm->get_active_flavor().bitmap_width_main)
        bm_data = cm->get_bm_data_main().data();
    else if (pbmi->bmiHeader.biWidth == cm->get_active_flavor().bitmap_width_settings)
        bm_data = cm->get_bm_data_settings().data();
    else if (pbmi->bmiHeader.biWidth == cm->get_active_flavor().bitmap_width_cassette)
        bm_data = cm->get_bm_data_cassette().data();

    if (bm_data != nullptr)
    {
        const auto bm_offset = reinterpret_cast<const BITMAPFILEHEADER*>(bm_data)->bfOffBits;
        const auto bm_handle = o_CreateDIBSection(hdc, pbmi, usage, &ppvBits_new, hSection, offset);

        memcpy(ppvBits_new, &bm_data[bm_offset], pbmi->bmiHeader.biSizeImage);

        return bm_handle;
    }

    return o_CreateDIBSection(hdc, pbmi, usage, ppvBits, hSection, offset);
}

/**
 * Is called on WM_PAINT messages
 * We hook this function to replace the window DC with our D2D memory DC
 * See https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-beginpaint
 */
HDC WINAPI hk_BeginPaint(HWND hWnd, LPPAINTSTRUCT lpPaint)
{
    if (wm->is_in_map(hWnd))
    {
        o_BeginPaint(hWnd, lpPaint);

        const auto& wctx = wm->get_wctx(hWnd);

        return wctx.mem_dc;
    }

    return o_BeginPaint(hWnd, lpPaint);
}

/**
 * Get a device context for the specified hwnd
 * We hook this function to replace the window DC with our D2D memory DC
 * See https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getdc
 */
HDC WINAPI hk_GetDC(HWND hWnd)
{
    if (wm->is_in_map(hWnd))
    {
        const auto& wctx = wm->get_wctx(hWnd);

        return wctx.mem_dc;
    }

    return o_GetDC(hWnd);
}

/**
 * Releases the device context for the specified hwnd
 * We hook this function to disable releasing our D2D memory DC
 * See https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-releasedc
 */
int WINAPI hk_ReleaseDC(HWND hWnd, HDC hdc)
{
    if (wm->is_in_map(hWnd))
        return 1;

    return o_ReleaseDC(hWnd, hdc);
}

/**
 * Retrieves the coordinates of a window's client area
 * We hook this function to fake the current window size to the application
 * See https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclientrect
 */
BOOL WINAPI hk_GetClientRect(HWND hWnd, LPRECT lpRect)
{
    // subwindows should think they have their default size
    auto class_name = std::wstring(256, '\0');
    const int len = GetClassNameW(hWnd, class_name.data(), 256);
    class_name.resize(len);

    if (class_name == window_manager::WDB_CLASSNAME_UNICODE)
    {
        lpRect->left = 0;
        lpRect->top = 0;
        lpRect->right = 100;
        lpRect->bottom = 386;
        return TRUE;
    }

    if (class_name == window_manager::COMPDENOISE_CLASSNAME_UNICODE)
    {
        lpRect->left = 0;
        lpRect->top = 0;
        lpRect->right = 153;
        lpRect->bottom = 413;
        return TRUE;
    }

    return o_GetClientRect(hWnd, lpRect);
}

/**
 * Changes the position of the window
 * We hook this function to disable the default drag behaviour since it's not compatible with the resizing feature and we have our own
 * See https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos
 */
BOOL WINAPI hk_SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags)
{
    if (hWnd == wm->get_hwnd_main() && GetAncestor(hWnd, GA_ROOT))
        return TRUE;

    return o_SetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

/**
 * Displays several popup menus
 * We hook this function to display the menus at the correct location when window is resized
 * See https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-trackpopupmenu
 */
BOOL WINAPI hk_TrackPopupMenu(HMENU hMenu, UINT uFlags, int x, int y, int nReserved, HWND hWnd, const RECT* prcRect)
{
    POINT pt = {x, y};

    if (hMenu != tray_menu && hWnd == wm->get_hwnd_main() && GetAncestor(hWnd, GA_ROOT))
    {
        ScreenToClient(hWnd, &pt);

        wm->scale_coords_inverse(hWnd, pt);

        ClientToScreen(hWnd, &pt);
    }

    return o_TrackPopupMenu(hMenu, uFlags, pt.x, pt.y, nReserved, hWnd, prcRect);
}

/**
 * Wndproc function of the main window
 * We hook this function to handle the resizing and render logic
 */
LRESULT ARCH_CALL hk_WndProc_main(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_COMMAND && LOWORD(wParam) == 0x1337)
        ShellExecuteW(nullptr, L"open", L"https://github.com/emkaix/voicemeeter-chroma", nullptr, nullptr, SW_SHOW);

    if (msg == WM_TIMER && wParam == 12346)
    {
        const auto ret = o_WndProc_main(hwnd, msg, wParam, lParam);
        wm->render(hwnd);
        return ret;
    }

    if (msg == WM_DISPLAYCHANGE)
    {
        const auto& wctx = wm->get_wctx(hwnd);

        SendMessageW(hwnd, WM_ERASEBKGND, reinterpret_cast<WPARAM>(wctx.mem_dc), lParam);
        SendMessageW(hwnd, WM_PAINT, 0, 0);
        return 0;
    }

    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK || msg == WM_LBUTTONUP || msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK || msg == WM_RBUTTONUP)
    {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        wm->scale_coords(hwnd, pt);

        const auto ret = o_WndProc_main(hwnd, msg, wParam, MAKELPARAM(pt.x, pt.y));

        if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK || msg == WM_LBUTTONUP)
            wm->render(hwnd);

        return ret;
    }

    if (msg == WM_MOUSEWHEEL)
    {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

        ScreenToClient(hwnd, &pt);

        wm->scale_coords(hwnd, pt);

        ClientToScreen(hwnd, &pt);

        const auto ret = o_WndProc_main(hwnd, msg, wParam, MAKELPARAM(pt.x, pt.y));

        wm->render(hwnd);

        return ret;
    }

    if (msg == WM_MOUSEMOVE)
    {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

        wm->scale_coords(hwnd, pt);

        const auto ret = o_WndProc_main(hwnd, msg, wParam, MAKELPARAM(pt.x, pt.y));

        // keep db meters from being visually stuck
        if (wParam & MK_LBUTTON)
            SendMessageA(hwnd, WM_TIMER, 12346, 0);

        return ret;
    }


    if (msg == WM_CREATE)
    {
        const auto cs = reinterpret_cast<const CREATESTRUCTA*>(lParam);

        wm->init_window(hwnd, WND_TYPE_MAIN, cs);

        wm->set_hwnd_main(hwnd);

        wm->set_default_main_wnd_size(cs->cx, cs->cy);

        uint32_t w, h;
        LRESULT ret;

        auto restore_size_opt = cm->cfg_get_restore_size();

        bool restore_size = true;

        if (restore_size_opt)
            restore_size = *restore_size_opt;

        if (restore_size && cm->reg_get_wnd_size(w, h))
        {
            wm->set_cur_main_wnd_size(w, h);

            ret = o_WndProc_main(hwnd, msg, wParam, lParam);

            o_SetWindowPos(hwnd, nullptr, cs->x, cs->y, w, h, SWP_NOREDRAW);

            wm->resize_d2d(hwnd, D2D1::SizeU(w, h));
        }
        else
        {
            wm->set_cur_main_wnd_size(cs->cx, cs->cy);

            ret = o_WndProc_main(hwnd, msg, wParam, lParam);
        }

#if defined(_WIN64)
        const auto o_scroll_handler_opt = utils::find_function_signature(sig_handle_scroll);

        if (!o_scroll_handler_opt)
        {
            SPDLOG_ERROR("unable to find mouse scroll handler function");
            return false;
        }

        o_scroll_handler = reinterpret_cast<o_scroll_handler_t>(*o_scroll_handler_opt);

        // patch mouse scroll instructions after integrity checks
        if (!utils::apply_scroll_patch64(o_scroll_handler))
        {
            SPDLOG_ERROR("unable to apply scroll patch");
            return ret;
        }

        if (!utils::hook_single_fn(&reinterpret_cast<PVOID&>(o_scroll_handler), hk_scroll_handler))
        {
            SPDLOG_ERROR("unable to hook scroll handler");
            return ret;
        }
#else

        const auto flavor_id = cm->get_current_flavor_id();
        const auto scroll_val = cm->cfg_get_fader_scroll_step();

        if (flavor_id && scroll_val)
        {
            if (!utils::apply_scroll_patch32(*flavor_id, *scroll_val))
            {
                SPDLOG_ERROR("unable to apply scroll patch");
                return ret;
            }
        }
#endif

        return ret;
    }

    if (msg == WM_NCHITTEST)
    {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &pt);

        RECT rc;
        o_GetClientRect(hwnd, &rc);

        constexpr int area_size = 10;

        if (pt.x > rc.right - area_size && pt.y > rc.bottom - area_size)
            return HTBOTTOMRIGHT;

        wm->scale_coords(hwnd, pt);

        const auto& af = cm->get_active_flavor();

        if (pt.x > af.htclient_x1 && pt.x < af.htclient_x2 && pt.y < 40)
            return HTCAPTION;

        return HTCLIENT;
    }

    if (msg == WM_SIZING)
    {
        const auto& wctx = wm->get_wctx(hwnd);

        if (wParam != WMSZ_BOTTOMRIGHT)
            return 0;

        const auto rect = reinterpret_cast<RECT*>(lParam);

        int new_width = rect->right - rect->left;
        new_width = max(wctx.default_cx / 2, min(wctx.default_cx, new_width));

        int new_height = MulDiv(new_width, wctx.default_cy, wctx.default_cx);
        new_height = max(wctx.default_cy / 2, min(wctx.default_cy, new_height));

        rect->right = rect->left + new_width;
        rect->bottom = rect->top + new_height;

        wm->set_cur_main_wnd_size(new_width, new_height);

        wm->resize_child_windows();

        SendMessageA(hwnd, WM_TIMER, 12346, 0);

        return 1;
    }

    if (msg == WM_SIZE)
    {
        const D2D1_SIZE_U size = {LOWORD(lParam), HIWORD(lParam)};

        wm->resize_d2d(hwnd, size);

        wm->set_cur_main_wnd_size(LOWORD(lParam), HIWORD(lParam));

        return o_WndProc_main(hwnd, msg, wParam, lParam);
    }

    if (msg == WM_PAINT)
    {
        const auto ret = o_WndProc_main(hwnd, msg, wParam, lParam);

        SendMessageA(hwnd, WM_TIMER, 12346, 0);

        wm->render(hwnd);

        return ret;
    }

    if (msg == WM_ERASEBKGND)
    {
        const auto& wctx = wm->get_wctx(hwnd);

        o_WndProc_main(hwnd, msg, reinterpret_cast<WPARAM>(wctx.mem_dc), lParam);

        return 1;
    }

    if (msg == WM_DESTROY)
    {
        RECT rc;
        o_GetClientRect(hwnd, &rc);

        const auto& wctx = wm->get_wctx(hwnd);

        if (rc.right > 0 && rc.right <= wctx.default_cx && rc.bottom > 0 && rc.bottom <= wctx.default_cy)
            cm->reg_save_wnd_size(rc.right, rc.bottom);

        wm->destroy_window(hwnd);
    }

    return o_WndProc_main(hwnd, msg, wParam, lParam);
}

/**
 * Wndproc function of the compressor / gate child window for potato
 * We hook this function to handle the resizing and render logic
 */
LRESULT WNDPROC_SUB_CALL hk_WndProc_comp(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, uint64_t a5)
{
    if (msg == WM_CREATE)
    {
        const auto cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        wm->init_window(hwnd, WND_TYPE_COMP_DENOISE, cs);

        o_SetTimer(hwnd, 12346, 15, nullptr);

        wm->scale_to_main_wnd(cs->x, cs->y, cs->cx, cs->cy);

        MoveWindow(hwnd, cs->x, cs->y, cs->cx, cs->cy, false);

        wm->resize_d2d(hwnd, D2D1::SizeU(cs->cx, cs->cy));

        return o_WndProc_comp(hwnd, msg, wParam, lParam, a5);
    }

    if (msg == WM_PAINT)
    {
        const auto ret = o_WndProc_comp(hwnd, msg, wParam, lParam, a5);

        wm->render(hwnd);

        return ret;
    }

    if (msg == WM_TIMER && wParam == 12346)
    {
        wm->render(hwnd);

        return 0;
    }

    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK || msg == WM_LBUTTONUP || msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK || msg == WM_RBUTTONUP)
    {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        wm->scale_coords(hwnd, pt);

        const auto ret = o_WndProc_comp(hwnd, msg, wParam, MAKELPARAM(pt.x, pt.y), a5);

        if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK || msg == WM_LBUTTONUP)
            wm->render(hwnd);

        return ret;
    }

    if (msg == WM_MOUSEMOVE)
    {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

        wm->scale_coords(hwnd, pt);

        const auto ret = o_WndProc_comp(hwnd, msg, wParam, MAKELPARAM(pt.x, pt.y), a5);

        if (wParam & MK_LBUTTON)
            wm->render(hwnd);

        return ret;
    }

    if (msg == WM_DESTROY)
    {
        const auto ret = o_WndProc_comp(hwnd, msg, wParam, lParam, a5);

        wm->destroy_window(hwnd);

        return ret;
    }

    if (msg == WM_ERASEBKGND)
    {
        const auto& wctx = wm->get_wctx(hwnd);

        return o_WndProc_comp(hwnd, msg, reinterpret_cast<WPARAM>(wctx.mem_dc), lParam, a5);
    }

    return o_WndProc_comp(hwnd, msg, wParam, lParam, a5);
}

/**
 * Wndproc function of the denoiser child window for potato
 * We hook this function to handle the resizing and render logic
 */
LRESULT WNDPROC_SUB_CALL hk_WndProc_denoiser(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, uint64_t a5)
{
    if (msg == WM_CREATE)
    {
        const auto cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        wm->init_window(hwnd, WND_TYPE_COMP_DENOISE, cs);

        o_SetTimer(hwnd, 12346, 15, nullptr);

        wm->scale_to_main_wnd(cs->x, cs->y, cs->cx, cs->cy);

        MoveWindow(hwnd, cs->x, cs->y, cs->cx, cs->cy, false);

        wm->resize_d2d(hwnd, D2D1::SizeU(cs->cx, cs->cy));

        return o_WndProc_denoiser(hwnd, msg, wParam, lParam, a5);
    }

    if (msg == WM_TIMER && wParam == 12346)
    {
        wm->render(hwnd);

        return 0;
    }

    if (msg == WM_PAINT)
    {
        const auto ret = o_WndProc_denoiser(hwnd, msg, wParam, lParam, a5);

        wm->render(hwnd);

        return ret;
    }

    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK || msg == WM_LBUTTONUP || msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK || msg == WM_RBUTTONUP)
    {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        wm->scale_coords(hwnd, pt);

        const auto ret = o_WndProc_denoiser(hwnd, msg, wParam, MAKELPARAM(pt.x, pt.y), a5);

        if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK || msg == WM_LBUTTONUP)
            wm->render(hwnd);

        return ret;
    }

    if (msg == WM_MOUSEMOVE)
    {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

        wm->scale_coords(hwnd, pt);

        auto ret = o_WndProc_denoiser(hwnd, msg, wParam, MAKELPARAM(pt.x, pt.y), a5);

        if (wParam & MK_LBUTTON)
            wm->render(hwnd);

        return ret;
    }

    if (msg == WM_DESTROY)
    {
        const auto ret = o_WndProc_denoiser(hwnd, msg, wParam, lParam, a5);

        wm->destroy_window(hwnd);

        return ret;
    }

    if (msg == WM_ERASEBKGND)
    {
        const auto& wctx = wm->get_wctx(hwnd);

        return o_WndProc_denoiser(hwnd, msg, reinterpret_cast<WPARAM>(wctx.mem_dc), lParam, a5);
    }

    return o_WndProc_denoiser(hwnd, msg, wParam, lParam, a5);
}

/**
 * Wndproc function of the windows app volume child window for potato
 * We hook this function to handle the resizing and render logic
 */
LRESULT WNDPROC_SUB_CALL hk_WndProc_wdb(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, uint64_t a5)
{
    if (msg == WM_PAINT)
    {
        const auto ret = o_WndProc_wdb(hwnd, msg, wParam, lParam, a5);

        wm->render(hwnd);

        return ret;
    }

    if (msg == WM_CREATE)
    {
        const auto cs = reinterpret_cast<CREATESTRUCTA*>(lParam);

        wm->init_window(hwnd, WND_TYPE_WDB, cs);

        o_SetTimer(hwnd, 12346, 15, nullptr);

        wm->scale_to_main_wnd(cs->x, cs->y, cs->cx, cs->cy);

        // fix pixel gap for wdb
        cs->x -= 1;
        cs->y -= 1;
        cs->cx += 2;
        cs->cy += 2;

        MoveWindow(hwnd, cs->x, cs->y, cs->cx, cs->cy, false);

        wm->resize_d2d(hwnd, D2D1::SizeU(cs->cx, cs->cy));

        return o_WndProc_wdb(hwnd, msg, wParam, lParam, a5);
    }

    if (msg == WM_TIMER && wParam == 12346)
    {
        wm->render(hwnd);

        return 0;
    }

    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK || msg == WM_LBUTTONUP || msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK || msg == WM_RBUTTONUP)
    {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        wm->scale_coords(hwnd, pt);

        const auto ret = o_WndProc_wdb(hwnd, msg, wParam, MAKELPARAM(pt.x, pt.y), a5);

        if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK || msg == WM_LBUTTONUP)
            wm->render(hwnd);

        return ret;
    }

    if (msg == WM_MOUSEMOVE)
    {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

        wm->scale_coords(hwnd, pt);

        const auto ret = o_WndProc_wdb(hwnd, msg, wParam, MAKELPARAM(pt.x, pt.y), a5);

        if (wParam & MK_LBUTTON)
            wm->render(hwnd);

        return ret;
    }

    if (msg == WM_DESTROY)
    {
        const auto ret = o_WndProc_wdb(hwnd, msg, wParam, lParam, a5);

        wm->destroy_window(hwnd);

        return ret;
    }

    if (msg == WM_ERASEBKGND)
    {
        const auto& wctx = wm->get_wctx(hwnd);
        return o_WndProc_wdb(hwnd, msg, reinterpret_cast<WPARAM>(wctx.mem_dc), lParam, a5);
    }

    return o_WndProc_wdb(hwnd, msg, wParam, lParam, a5);
}

/**
 * We hook this function in order to get the address of WndProc from the lpWndClass pointer, so we can hook WndProc
 * See https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerclassa
 */
ATOM WINAPI hk_RegisterClassA(const WNDCLASSA* lpWndClass)
{
    if (lpWndClass->lpszClassName == window_manager::MAINWINDOW_CLASSNAME)
    {
        o_WndProc_main = lpWndClass->lpfnWndProc;

        if (!utils::hook_single_fn(&reinterpret_cast<PVOID&>(o_WndProc_main), hk_WndProc_main))
        {
            SPDLOG_ERROR("failed to hook main wndproc");
        }
    }

    return o_RegisterClassA(lpWndClass);
}

/**
 * Called when a new window is created
 * We hook this function to check for child window creation (potato) for late hooking, since the wndproc pointer is passed
 */
HWND WINAPI hk_CreateWindowExA(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
    if (lpParam == nullptr)
        return o_CreateWindowExA(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);

    auto lparam_info = static_cast<createwindowexa_lparam_t*>(lpParam);

    std::string class_name = lpClassName;

    // denoiser window
    if (class_name == window_manager::COMPDENOISE_CLASSNAME_ANSI && o_WndProc_denoiser == nullptr && lparam_info->wnd_id >= 1200 && lparam_info->wnd_id <= 1204)
    {
        o_WndProc_denoiser = reinterpret_cast<o_WndProc_chldwnd_t>(lparam_info->wndproc);
        if (!utils::hook_single_fn(&reinterpret_cast<PVOID&>(o_WndProc_denoiser), hk_WndProc_denoiser))
        {
            SPDLOG_ERROR("failed to hook denoiser wndproc");
        }
    }

    // compressor window
    if (class_name == window_manager::COMPDENOISE_CLASSNAME_ANSI && o_WndProc_comp == nullptr && lparam_info->wnd_id >= 1100 && lparam_info->wnd_id <= 1104)
    {
        o_WndProc_comp = reinterpret_cast<o_WndProc_chldwnd_t>(lparam_info->wndproc);
        if (!utils::hook_single_fn(&reinterpret_cast<PVOID&>(o_WndProc_comp), hk_WndProc_comp))
        {
            SPDLOG_ERROR("failed to hook compressor wndproc");
        }
    }

    // wdb window
    if (class_name == window_manager::WDB_CLASSNAME_ANSI && o_WndProc_wdb == nullptr && lparam_info->wnd_id >= 1000 && lparam_info->wnd_id <= 1002)
    {
        o_WndProc_wdb = reinterpret_cast<o_WndProc_chldwnd_t>(lparam_info->wndproc);
        if (!utils::hook_single_fn(&reinterpret_cast<PVOID&>(o_WndProc_wdb), hk_WndProc_wdb))
        {
            SPDLOG_ERROR("failed to hook wdb wndproc");
        }
    }

    return o_CreateWindowExA(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
}

/**
 * Displays the small "edit" boxes when a fader or label is right clicked
 * We hook this function so that the boxes are displayed at the correct location when window is resized
 * See https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-dialogboxindirectparama
 */
INT_PTR WINAPI hk_DialogBoxIndirectParamA(HINSTANCE hInstance, LPCDLGTEMPLATEA hDialogTemplate, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam)
{
    if (dwInitParam == 0)
        return o_DialogBoxIndirectParamA(hInstance, hDialogTemplate, hWndParent, lpDialogFunc, dwInitParam);

    auto lparam = reinterpret_cast<dialogbox_initparam_t*>(dwInitParam);

    // 2016 is some magic value for all the "edit" dialogs
    if (hWndParent == wm->get_hwnd_main() && lparam->unk2 == 2016)
    {
        POINT pt = {lparam->x, lparam->y};
        ScreenToClient(hWndParent, &pt);

        wm->scale_coords_inverse(hWndParent, pt);

        ClientToScreen(hWndParent, &pt);

        lparam->x = pt.x;
        lparam->y = pt.y;
    }

    return o_DialogBoxIndirectParamA(hInstance, hDialogTemplate, hWndParent, lpDialogFunc, dwInitParam);
}

//*****************************//
//        DETOURS SETUP        //
//*****************************//

static std::vector<std::pair<PVOID*, PVOID>> hooks_base = {
    {&reinterpret_cast<PVOID&>(o_AppendMenuA), hk_AppendMenuA},
    {&reinterpret_cast<PVOID&>(o_RegisterClassA), hk_RegisterClassA},
    {&reinterpret_cast<PVOID&>(o_Rectangle), hk_Rectangle},
    {&reinterpret_cast<PVOID&>(o_BeginPaint), hk_BeginPaint},
    {&reinterpret_cast<PVOID&>(o_SetTimer), hk_SetTimer},
    {&reinterpret_cast<PVOID&>(o_GetDC), hk_GetDC},
    {&reinterpret_cast<PVOID&>(o_ReleaseDC), hk_ReleaseDC},
    {&reinterpret_cast<PVOID&>(o_SetWindowPos), hk_SetWindowPos},
    {&reinterpret_cast<PVOID&>(o_CreateWindowExA), hk_CreateWindowExA},
    {&reinterpret_cast<PVOID&>(o_DialogBoxIndirectParamA), hk_DialogBoxIndirectParamA},
    {&reinterpret_cast<PVOID&>(o_TrackPopupMenu), hk_TrackPopupMenu},
    {&reinterpret_cast<PVOID&>(o_GetClientRect), hk_GetClientRect},
};

static std::vector<std::pair<PVOID*, PVOID>> hooks_theme = {
    {&reinterpret_cast<PVOID&>(o_CreateFontIndirectA), hk_CreateFontIndirectA},
    {&reinterpret_cast<PVOID&>(o_CreatePen), hk_CreatePen},
    {&reinterpret_cast<PVOID&>(o_CreateBrushIndirect), hk_CreateBrushIndirect},
    {&reinterpret_cast<PVOID&>(o_SetTextColor), hk_SetTextColor},
    {&reinterpret_cast<PVOID&>(o_CreateDIBSection), hk_CreateDIBSection},
};

/**
 * Initializes Detours hooks
 * @return True if hooks are attached successfully, false otherwise
 */
bool apply_hooks()
{
    if (DetourTransactionBegin() != NO_ERROR)
        return false;

    if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR)
        return false;

    for (const auto& [original, hook] : hooks_base)
    {
        if (*original != nullptr && DetourAttach(original, hook) != NO_ERROR)
        {
            SPDLOG_ERROR("unable to hook functions");
            return false;
        }
    }

    if (cm->get_theme_enabled())
    {
        for (const auto& [original, hook] : hooks_theme)
        {
            if (*original != nullptr && DetourAttach(original, hook) != NO_ERROR)
            {
                SPDLOG_ERROR("unable to hook functions");
                return false;
            }
        }
    }

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
        return utils::hook_single_fn(&reinterpret_cast<PVOID&>(o_CreateMutexA), hk_CreateMutexA);
    }

    return TRUE;
}
