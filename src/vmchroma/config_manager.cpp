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

#include "config_manager.hpp"

#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include "window_manager.hpp"
#include "yaml-cpp/yaml.h"

/**
 * Saves the current window dimensions to the windows registry
 * @param width Current Width
 * @param height Current Height
 */
void config_manager::reg_save_wnd_size(uint32_t width, uint32_t height)
{
    std::wstring sub_key;
    const auto cur_flavor = get_current_flavor_id();

    if (!cur_flavor)
    {
        SPDLOG_ERROR("error getting current flavor");
        return;
    }

    if (*cur_flavor == FLAVOR_POTATO)
        sub_key = reg_sub_key_potato;

    if (*cur_flavor == FLAVOR_BANANA)
        sub_key = reg_sub_key_banana;

    if (*cur_flavor == FLAVOR_DEFAULT)
        sub_key = reg_sub_key_default;

    HKEY hKey;
    auto result = RegCreateKeyExW(HKEY_CURRENT_USER, sub_key.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &hKey, nullptr);

    if (result != ERROR_SUCCESS)
    {
        SPDLOG_ERROR("error open registry key");
        return;
    }

    result = RegSetValueExW(hKey, reg_val_wnd_size_width.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&width), sizeof(DWORD));

    if (result != ERROR_SUCCESS)
    {
        SPDLOG_ERROR("error writing registry key");
        return;
    }

    result = RegSetValueExW(hKey, reg_val_wnd_size_height.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&height), sizeof(DWORD));

    if (result != ERROR_SUCCESS)
    {
        SPDLOG_ERROR("error writing registry key");
        return;
    }

    RegCloseKey(hKey);
}

/**
 * Queries the last window dimension from the windows registry
 * @param width Last width
 * @param height Last Height
 * @return
 */
bool config_manager::reg_get_wnd_size(uint32_t& width, uint32_t& height)
{
    std::wstring sub_key;
    auto cur_flavor = get_current_flavor_id();

    if (cur_flavor == FLAVOR_NONE)
    {
        SPDLOG_ERROR("error getting current flavor");
        return false;
    }

    if (cur_flavor == FLAVOR_POTATO)
        sub_key = reg_sub_key_potato;

    if (cur_flavor == FLAVOR_BANANA)
        sub_key = reg_sub_key_banana;

    if (cur_flavor == FLAVOR_DEFAULT)
        sub_key = reg_sub_key_default;

    HKEY hKey;
    auto result = RegOpenKeyExW(HKEY_CURRENT_USER, sub_key.c_str(), 0, KEY_READ, &hKey);

    // key doesn't exist yet
    if (result == ERROR_FILE_NOT_FOUND)
        return false;

    if (result != ERROR_SUCCESS)
    {
        SPDLOG_ERROR("Error opening registry key: {}", result);
        return false;
    }

    DWORD dataSize = sizeof(DWORD);
    DWORD type;

    result = RegQueryValueExW(hKey, reg_val_wnd_size_width.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(&width), &dataSize);

    if (result != ERROR_SUCCESS || type != REG_DWORD || dataSize != sizeof(DWORD))
    {
        SPDLOG_ERROR("Error reading width registry value: {}", result);
        RegCloseKey(hKey);
        return false;
    }

    result = RegQueryValueExW(hKey, reg_val_wnd_size_height.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(&height), &dataSize);

    if (result != ERROR_SUCCESS || type != REG_DWORD || dataSize != sizeof(DWORD))
    {
        SPDLOG_ERROR("Error reading height registry value: {}", result);
        RegCloseKey(hKey);
        return false;
    }

    RegCloseKey(hKey);
    return true;
}

/**
 * Queries the current Voicemeeter version by reading the version info of the executable
 * @return The current Voicemeeter flavor
 */
std::optional<flavor_id> config_manager::get_current_flavor_id()
{
    if (current_flavor_id != FLAVOR_NONE)
        return current_flavor_id;

    std::wstring executable_name(MAX_PATH, '\0');

    if (!GetModuleFileName(nullptr, executable_name.data(), MAX_PATH))
    {
        SPDLOG_ERROR("GetModuleFileName failed");
        return std::nullopt;
    }

    DWORD dummy;
    DWORD versionInfoSize = GetFileVersionInfoSize(executable_name.c_str(), &dummy);

    if (versionInfoSize == 0)
    {
        SPDLOG_ERROR("GetFileVersionInfoSize returned 0");
        return std::nullopt;
    }

    std::vector<char> versionInfo(versionInfoSize);

    if (!GetFileVersionInfo(executable_name.c_str(), 0, versionInfoSize, versionInfo.data()))
    {
        SPDLOG_ERROR("GetFileVersionInfo failed");
        return std::nullopt;
    }

    LPVOID value = nullptr;
    UINT valueLen = 0;
    const std::wstring query = L"\\StringFileInfo\\000004b0\\ProductName";

    if (!VerQueryValue(versionInfo.data(), query.data(), &value, &valueLen) || valueLen <= 0)
    {
        SPDLOG_ERROR("VerQueryValue failed");
        return std::nullopt;
    }

    std::wstring product_name = static_cast<wchar_t*>(value);

    if (product_name == L"VoiceMeeter")
    {
        current_flavor_id = FLAVOR_DEFAULT;
        return FLAVOR_DEFAULT;
    }

    if (product_name == L"VoiceMeeter Banana")
    {
        current_flavor_id = FLAVOR_BANANA;
        return FLAVOR_BANANA;
    }

    if (product_name == L"VoiceMeeter Potato")
    {
        current_flavor_id = FLAVOR_POTATO;
        return FLAVOR_POTATO;
    }

    SPDLOG_ERROR("no product name matched");
    return std::nullopt;
}

/**
 * Loads the theme bitmap data from the theme directory
 * @return True if loading was successful
 */
bool config_manager::init_theme()
{
    auto flavor_id = get_current_flavor_id();

    if (!flavor_id)
    {
        SPDLOG_ERROR("can't get Voicemeeter flavor from version info");
        return false;
    }

    active_flavor = flavor_map[*flavor_id];

    // no theme specified
    if (!yaml_config["theme"][active_flavor.name].IsScalar())
    {
        theme_enabled = false;
        return true;
    }

    std::string active_theme_name_str;
    try
    {
        active_theme_name_str = yaml_config["theme"][active_flavor.name].as<std::string>();
    }
    catch (YAML::TypedBadConversion<std::string>&)
    {
        SPDLOG_ERROR("error parsing theme name");
        return false;
    }

    auto active_theme_name_wstr = utils::str_to_wstr(active_theme_name_str);

    if (!active_theme_name_wstr)
    {
        SPDLOG_ERROR("active_theme_name_str conversion error");
        return false;
    }

    auto active_flavor_name = utils::str_to_wstr(active_flavor.name);

    if (!active_flavor_name)
    {
        SPDLOG_ERROR("active_flavor.name conversion error");
        return false;
    }

    auto userprofile_path = utils::get_userprofile_path();

    std::wstring theme_path = (std::filesystem::path(*userprofile_path) / L"themes" / *active_theme_name_wstr / *active_flavor_name);

    if (!std::filesystem::exists(std::filesystem::path(theme_path) / BM_FILE_BG))
    {
        SPDLOG_ERROR("can't find {} in themes folder", *utils::wstr_to_str(BM_FILE_BG));
        return false;
    }

    if (!utils::load_bitmap(std::filesystem::path(theme_path) / BM_FILE_BG, bg_main_bitmap_data))
    {
        SPDLOG_ERROR("error loading {}", *utils::wstr_to_str(BM_FILE_BG));
        return false;
    }

    if (!std::filesystem::exists(std::filesystem::path(theme_path) / BM_FILE_BG_SETTINGS))
    {
        SPDLOG_ERROR("can't find {} in themes folder", *utils::wstr_to_str(BM_FILE_BG_SETTINGS));
        return false;
    }

    if (!utils::load_bitmap(std::filesystem::path(theme_path) / BM_FILE_BG_SETTINGS, bg_settings_bitmap_data))
    {
        SPDLOG_ERROR("error loading {}", *utils::wstr_to_str(BM_FILE_BG_SETTINGS));
        return false;
    }

    if (!std::filesystem::exists(std::filesystem::path(theme_path) / BM_FILE_BG_CASSETTE))
    {
        SPDLOG_ERROR("can't find {} in themes folder", *utils::wstr_to_str(BM_FILE_BG_CASSETTE));
        return false;
    }

    if (!utils::load_bitmap(std::filesystem::path(theme_path) / BM_FILE_BG_CASSETTE, bg_cassette_bitmap_data))
    {
        SPDLOG_ERROR("error loading {}", *utils::wstr_to_str(BM_FILE_BG_CASSETTE));
        return false;
    }

    if (!std::filesystem::exists(std::filesystem::path(*userprofile_path) / L"themes" / *active_theme_name_wstr / CONFIG_FILE_COLORS))
    {
        SPDLOG_ERROR("can't find {}", *utils::wstr_to_str(CONFIG_FILE_COLORS));
        return false;
    }

    std::ifstream colors_file(std::filesystem::path(*userprofile_path) / L"themes" / *active_theme_name_wstr / CONFIG_FILE_COLORS);

    if (!colors_file.is_open())
    {
        SPDLOG_ERROR("can't open {}", *utils::wstr_to_str(CONFIG_FILE_COLORS));
        return false;
    }

    try
    {
        yaml_colors = YAML::Load(colors_file);
    }
    catch (YAML::ParserException&)
    {
        SPDLOG_ERROR("failed to parse {}", *utils::wstr_to_str(CONFIG_FILE_COLORS));
        return false;
    }

    return true;
}

/**
 * Loads the config file vmchroma.yaml
 * @return True if loading was successful
 */
bool config_manager::load_config()
{
    auto userprofile_path = utils::get_userprofile_path();

    if (!userprofile_path)
    {
        SPDLOG_ERROR("can't get userprofile path");
        return false;
    }

    if (!std::filesystem::exists(std::filesystem::path(*userprofile_path) / CONFIG_FILE_THEME))
    {
        SPDLOG_ERROR("{} not found", *utils::wstr_to_str(CONFIG_FILE_THEME));
        return false;
    }

    std::ifstream cfg_file(std::filesystem::path(*userprofile_path) / CONFIG_FILE_THEME);

    if (!cfg_file.is_open())
    {
        SPDLOG_ERROR("can't open {}", *utils::wstr_to_str(CONFIG_FILE_THEME));
        return false;
    }

    try
    {
        yaml_config = YAML::Load(cfg_file);
    }
    catch (YAML::ParserException&)
    {
        SPDLOG_ERROR("failed to parse {}", *utils::wstr_to_str(CONFIG_FILE_THEME));
        return false;
    }

    return true;
}

/**
 * Gets the font quality value from the config
 * @return Font quality value
 */
std::optional<uint32_t> config_manager::cfg_get_font_quality()
{
    if (!yaml_config["misc"]["fontQuality"].IsScalar())
    {
        SPDLOG_ERROR("missing fontQuality value");
        return std::nullopt;
    }

    try
    {
        auto val = yaml_config["misc"]["fontQuality"].as<uint32_t>();

        if (val > 6)
        {
            SPDLOG_ERROR("fontQuality value must be between 0 and 6");
            return std::nullopt;
        }

        return val;
    }
    catch (YAML::TypedBadConversion<uint32_t>&)
    {
        SPDLOG_ERROR("error fontQuality value");
        return std::nullopt;
    }
}

/**
 * Gets the fader shift scroll value from the config
 * @return Fader shift scroll value
 */
std::optional<uint32_t> config_manager::cfg_get_fader_shift_scroll_step()
{
    if (!yaml_config["misc"]["faderShiftScrollStep"].IsScalar())
    {
        SPDLOG_ERROR("missing faderShiftScrollStep value");
        return std::nullopt;
    }

    try
    {
        return yaml_config["misc"]["faderShiftScrollStep"].as<uint32_t>();
    }
    catch (YAML::TypedBadConversion<uint32_t>&)
    {
        SPDLOG_ERROR("error faderShiftScrollStep value");
        return std::nullopt;
    }
}

/**
 * Gets the fader scroll value from the config
 * @return Fader scroll value
 */
std::optional<uint32_t> config_manager::cfg_get_fader_scroll_step()
{
    if (!yaml_config["misc"]["faderScrollStep"].IsScalar())
    {
        SPDLOG_ERROR("missing faderScrollStep value");
        return std::nullopt;
    }

    try
    {
        return yaml_config["misc"]["faderScrollStep"].as<uint32_t>();
    }
    catch (YAML::TypedBadConversion<uint32_t>&)
    {
        SPDLOG_ERROR("error faderScrollStep value");
        return std::nullopt;
    }

}

/**
 * Gets the UI update interval from the config
 * @return UI update interval value
 */
std::optional<uint32_t> config_manager::cfg_get_ui_update_interval()
{
    if (!yaml_config["misc"]["updateIntervalUI"].IsScalar())
    {
        SPDLOG_ERROR("missing updateIntervalUI value");
        return std::nullopt;
    }

    try
    {
        return yaml_config["misc"]["updateIntervalUI"].as<uint32_t>();
    }
    catch (YAML::TypedBadConversion<uint32_t>&)
    {
        SPDLOG_ERROR("error updateIntervalUI value");
        return std::nullopt;
    }
}

/**
 * Gets the "restore size on start" value from the config
 * @return "restore size on start" value
 */
std::optional<bool> config_manager::cfg_get_restore_size()
{
    if (!yaml_config["misc"]["restoreSize"].IsScalar())
    {
        SPDLOG_ERROR("missing restoreSize value");
        return std::nullopt;
    }

    try
    {
        return yaml_config["misc"]["restoreSize"].as<bool>();
    }
    catch (YAML::TypedBadConversion<uint32_t>&)
    {
        SPDLOG_ERROR("error restoreSize value");
        return std::nullopt;
    }
}

/**
 * Gets a color value from the yaml file in a case-insensitive way
 * @param arg_col The color value in upper case
 * @param category Can either be "shapes" or "text"
 * @return The mapped color value for the current theme
 */
std::optional<std::string> config_manager::cfg_get_color(const std::string& arg_col, const color_category& category)
{
    YAML::Node category_node;

    if (category == CATEGORY_SHAPES)
        category_node = yaml_colors["shapes"];

    if (category == CATEGORY_TEXT)
        category_node = yaml_colors["text"];

    for (auto it = category_node.begin(); it != category_node.end(); ++it)
    {
        auto current_color = it->first.as<std::string>();
        std::string current_color_upper(current_color.size(), '\0');
        std::transform(current_color.begin(), current_color.end(), current_color_upper.begin(), ::toupper);

        if (current_color_upper == arg_col)
        {
            auto ret = it->second.as<std::string>();

            if (ret.empty())
                return std::nullopt;

            return ret;
        }
    }

    return std::nullopt;
}

const std::vector<uint8_t>& config_manager::get_bm_data_main()
{
    return bg_main_bitmap_data;
}

const std::vector<uint8_t>& config_manager::get_bm_data_settings()
{
    return bg_settings_bitmap_data;
}

const std::vector<uint8_t>& config_manager::get_bm_data_cassette()
{
    return bg_cassette_bitmap_data;
}

const flavor_info_t& config_manager::get_active_flavor()
{
    return active_flavor;
}

bool config_manager::get_theme_enabled()
{
    return theme_enabled;
}
