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

static std::string dll_path;

static BOOL CALLBACK AddBywayCallback(PVOID pContext, LPCSTR pszFile, LPCSTR* ppszOutFile)
{
    auto pbAddedDll = static_cast<PBOOL>(pContext);

    if (!pszFile && !*pbAddedDll)
    {
        *pbAddedDll = TRUE;
        *ppszOutFile = dll_path.c_str();
    }

    return TRUE;
}

int main(int argc, char** argv)
{
    if (argc != 4)
        exit(ERROR_INVALID_PARAMETER);

    dll_path = argv[1];
    std::string exe_path_old = argv[2];
    std::string exe_path_new = argv[3];

    HANDLE handle_exe_old = CreateFileA(exe_path_old.c_str(),
                                    GENERIC_READ,
                                    FILE_SHARE_READ,
                                    nullptr,
                                    OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr);

    if (handle_exe_old == INVALID_HANDLE_VALUE)
    {
        printf("couldn't open input file: %s, error: %d\n", exe_path_old.c_str(), GetLastError());
        exit(1);
    }

    PDETOUR_BINARY binary = DetourBinaryOpen(handle_exe_old);
    CloseHandle(handle_exe_old);

    if (!binary)
    {
        printf("action failed: %d\n", GetLastError());
        exit(1);
    }

    DetourBinaryResetImports(binary);
    BOOL bAddedDll = FALSE;

    if (!DetourBinaryEditImports(binary, &bAddedDll, AddBywayCallback, nullptr, nullptr, nullptr))
    {
        printf("action failed: %d\n", GetLastError());
        DetourBinaryClose(binary);
        exit(1);
    }

    HANDLE handle_exe_new = CreateFileA(exe_path_new.c_str(),
                             GENERIC_READ | GENERIC_WRITE,
                             0,
                             nullptr,
                             CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                             nullptr);

    if (handle_exe_new == INVALID_HANDLE_VALUE)
    {
        printf("couldn't open input file: %s, error: %d\n", exe_path_new.c_str(), GetLastError());
        exit(1);
    }

    if (!DetourBinaryWrite(binary, handle_exe_new)) {
        printf("action failed: %d\n", GetLastError());
        DetourBinaryClose(binary);
        CloseHandle(handle_exe_new);
        exit(1);
    }

    DetourBinaryClose(binary);
    CloseHandle(handle_exe_new);
    printf("success: %s\n", exe_path_old.c_str());
    return 0;
}
