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
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
    if (size == 0)
        return std::nullopt;

    std::wstring res(size, 0);
    if (MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), res.data(), size) == 0)
        return std::nullopt;

    return res;
}

/**
 * Converts a std::wstring to UTF-8 encoded std::string
 * @param wstr Wide string for conversion
 * @return Converted narrow string
 */
std::optional<std::string> wstr_to_str(const std::wstring& wstr)
{
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size == 0)
        return std::nullopt;

    std::string res(size - 1, 0); // size includes null terminator, subtract 1
    if (WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, res.data(), size, nullptr, nullptr) == 0)
        return std::nullopt;

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
        spdlog::error("empty hex value passed to hex_to_colorref");
        return std::nullopt;
    }

    std::string clean_hex = (hex[0] == '#') ? hex.substr(1) : hex;

    if (clean_hex.length() != 6)
    {
        spdlog::error("invalid value passed to hex_to_colorref: {}", hex);
        return std::nullopt;
    }

    unsigned long value = 0;

    try
    {
        value = std::stoul(clean_hex, nullptr, 16);
    }
    catch (...)
    {
        spdlog::error("invalid hex value passed to hex_to_colorref: {}", clean_hex);
        return std::nullopt;
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
std::optional<PVOID> find_function_signature(const signature_t& sig)
{
    HMODULE h_module = GetModuleHandle(nullptr);
    MODULEINFO mod_info;

    if (!h_module)
    {
        spdlog::error("GetModuleHandle failed");
        return std::nullopt;
    }

    if (!GetModuleInformation(GetCurrentProcess(), h_module, &mod_info, sizeof(mod_info)))
    {
        spdlog::error("GetModuleInformation failed");
        return std::nullopt;
    }

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

    spdlog::error("signature scan exhausted");
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
        spdlog::error("failed to open file {}", *wstr_to_str(path));
        return false;
    }

    std::streampos size = f.tellg();
    f.seekg(0, std::ios::beg);
    target.assign(size, '\0');

    if (!f.read(reinterpret_cast<char*>(target.data()), size))
    {
        spdlog::error("failed to read file {}", *wstr_to_str(path));
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

    HRESULT res = SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &buffer);

    if (res != S_OK)
    {
        spdlog::error("SHGetKnownFolderPath failed: {}", res);
        return std::nullopt;
    }

    std::wstring userprofile_path = buffer;
    std::wstring result = std::filesystem::path(userprofile_path) / L"Voicemeeter";
    CoTaskMemFree(buffer);

    return result;
}

/**
 * Gets a color value from the json in a case-insensitive way
 * @param yaml_colors Loaded json data
 * @param arg_col The color value in upper case
 * @param category Can either be "shapes" or "text"
 * @return The mapped color value for the current theme
 */
std::optional<std::string> get_yaml_color(const YAML::Node& yaml_colors, const std::string& arg_col, const color_category& category)
{
    YAML::Node category_node;

    if (category == CATEGORY_SHAPES)
        category_node = yaml_colors["shapes"];

    if (category == CATEGORY_TEXT)
        category_node = yaml_colors["text"];

    for (YAML::const_iterator it = category_node.begin(); it != category_node.end(); ++it)
    {
        std::string current_color = it->first.as<std::string>();
        std::string current_color_upper(current_color.size(), '\0');
        std::transform(current_color.begin(), current_color.end(), current_color_upper.begin(), ::toupper);

        if (current_color_upper == arg_col)
        {
            std::string ret = it->second.as<std::string>();
            if (ret.empty())
                return std::nullopt;

            return ret;
        }
    }

    return std::nullopt;
}

/**
 * Queries the version info embedded in the Voicemeeter executable to get the ProductName property and returns the corresponding flavor ID
 * @return Flavor ID that corresponds to the current Voicemeeter executable
 */
std::optional<flavor_id> get_flavor_id()
{
    std::wstring executable_name(MAX_PATH, '\0');

    if (!GetModuleFileName(nullptr, executable_name.data(), MAX_PATH))
    {
        spdlog::error("GetModuleFileName failed");
        return std::nullopt;
    }

    DWORD dummy;
    DWORD versionInfoSize = GetFileVersionInfoSize(executable_name.c_str(), &dummy);

    if (versionInfoSize == 0)
    {
        spdlog::error("GetFileVersionInfoSize returned 0");
        return std::nullopt;
    }

    std::vector<char> versionInfo(versionInfoSize);

    if (!GetFileVersionInfo(executable_name.c_str(), 0, versionInfoSize, versionInfo.data()))
    {
        spdlog::error("GetFileVersionInfo failed");
        return std::nullopt;
    }

    LPVOID value = nullptr;
    UINT valueLen = 0;
    std::wstring query = L"\\StringFileInfo\\000004b0\\ProductName";

    if (!VerQueryValue(versionInfo.data(), query.data(), &value, &valueLen) || valueLen <= 0)
    {
        spdlog::error("VerQueryValue failed");
        return std::nullopt;
    }

    std::wstring product_name = static_cast<wchar_t*>(value);

    if (product_name == L"VoiceMeeter")
        return FLAVOR_DEFAULT;

    if (product_name == L"VoiceMeeter Banana")
        return FLAVOR_BANANA;

    if (product_name == L"VoiceMeeter Potato")
        return FLAVOR_POTATO;

    return std::nullopt; // Return empty string if not found
}

/**
 * Initializes logging library to log to a file
 */
void setup_logging()
{
    auto userprofile_path_wstr = get_userprofile_path();

    if (!userprofile_path_wstr)
        mbox_error(L"failed to get user profile path in setup_logging");

    auto userprofile_path = wstr_to_str(*userprofile_path_wstr);

    if (!userprofile_path)
        mbox_error(L"string conversion error in setup_logging");

    std::string log_file_path = (std::filesystem::path(*userprofile_path) / "themes" / "vmtheme_log.txt").string();

    try
    {
        auto logger = spdlog::rotating_logger_mt("vmtheme_logger", log_file_path, 1048576 * 5, 1);
        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%d.%m.%Y %H:%M:%S] [%L] %v");
        spdlog::flush_on(spdlog::level::info);
    }
    catch (const spdlog::spdlog_ex& ex)
    {
        mbox_error(L"logger setup error");
    }
}
}
