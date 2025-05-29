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

#pragma once

#include <windows.h>
#include <detours.h>
#include <string>
#include <optional>
#include <vector>
#include <detours.h>
#include <fstream>
#include <shlwapi.h>
#include <filesystem>
#include <algorithm>
#include <iomanip>
#include <psapi.h>
#include <shlobj.h>
#include <sstream>
#include <iostream>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <yaml-cpp/yaml.h>

typedef struct signature
{
    const std::vector<uint8_t> pattern;
    const char* mask;
} signature_t;

const enum flavor_id { FLAVOR_DEFAULT, FLAVOR_BANANA, FLAVOR_POTATO };

const enum color_category { CATEGORY_TEXT, CATEGORY_SHAPES };

typedef struct flavor_info
{
    std::string name;
    flavor_id id;
    uint32_t bitmap_size_main{};
    uint32_t bitmap_size_settings{};
} flavor_info_t;

namespace utils
{
void mbox(const std::wstring&);
void mbox_error(const std::wstring&);
void attach_console_debug();
std::optional<std::wstring> str_to_wstr(const std::string&);
std::optional<std::string> wstr_to_str(const std::wstring&);
std::string colorref_to_hex(COLORREF);
std::optional<COLORREF> hex_to_colorref(const std::string&);
std::optional<PVOID> find_function_signature(const signature_t&);
bool load_bitmap(const std::wstring&, std::vector<uint8_t>&);
std::optional<std::wstring> get_userprofile_path();
std::optional<std::string> get_yaml_color(const YAML::Node&, const std::string&, const color_category&);
std::optional<flavor_id> get_flavor_id();
void setup_logging();

/**
 * Allocate console to print debug messages
 */
inline void attach_console_debug()
{
#ifndef NDEBUG
    mbox(L"attached");
    if (!AllocConsole())
        mbox_error(L"AllocConsole");

    FILE* fDummy;
    if (freopen_s(&fDummy, "CONOUT$", "w", stdout) != ERROR_SUCCESS)
        mbox_error(L"freopen_s");
#endif
}
}
