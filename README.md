# Voicemeeter Themes Mod

Enables theming support for Voicemeeter, Voicemeeter Banana and Voicemeeter Potato.

![UI](https://raw.githubusercontent.com/emkaix/voicemeeter-theme-catppuccin-mocha/refs/heads/main/potato.png)
![UI](https://github.com/emkaix/voicemeeter-theme-catppuccin-macchiato/blob/main/banana.png?raw=true)

- [Disclaimer](#disclaimer)
- [Status](#status)
- [Getting started](#getting-started)
- [Supported themes](#supported-themes)
- [Build](#build)
- [FAQ](#frequently-asked-questions)
- [Dependencies](#dependencies)

## Disclaimer

:heavy_exclamation_mark: **This project is an unofficial, third-party, independent modification of Voicemeeter. It is not affiliated with, endorsed by, or in any way officially connected to VB-Audio Software or any of its subsidiaries or affiliates. All trademarks, service marks, and copyrights related to Voicemeeter are the property of VB-Audio Software.**


:heavy_exclamation_mark: **If you have any problems related to or caused by this mod, you will not get any support whatsoever on the Voicemeeter forums or the Voicemeeter Discord.**

## Status

Tested with Windows 10/11 and versions `1.1.1.9`, `2.1.1.9`, `3.1.1.9` of the software (older versions may work as well).

## Getting started

1. [Build the project from source](#build).
2. Copy `vmtheme64.dll`/`vmtheme32.dll` into `C:\Program Files (x86)\VB\Voicemeeter\`
3. Choose a [supported theme](#supported-themes) and place it in `C:\Users\<USER>\Documents\Voicemeeter\themes\`
4. Set the active theme in `C:\Users\<USER>\Documents\Voicemeeter\theme.json`.
5. Launch Voicemeeter with the launcher. The name depends on your build target, for Banana x64 the name is `vmtheme64-banana.exe`.

## Supported themes

[Catppucchin Macchiato (currently only Banana)](https://github.com/emkaix/voicemeeter-theme-catppuccin-macchiato)

[Catppucchin Mocha (Banana & Potato)](https://github.com/emkaix/voicemeeter-theme-catppuccin-mocha)

## Build

Install the following toolchain if you don't have it installed already (older versions of the toolchain may work as well). After building, you can uninstall everything, if you want.

1. Download and run [Build Tools for Visual Studio 2022](https://visualstudio.microsoft.com/downloads/?q=build+tools#build-tools-for-visual-studio-2022).
2. Under "Individual components" search for and select the following:
   - :ballot_box_with_check: `MSVC v143 - VS 2022 C++ x64/x86 build tools (Latest)`
   - :ballot_box_with_check: `Windows 11 SDK (10.0.22621.0)`
   - :ballot_box_with_check: `C++ CMake tools for Windows`
3. Make sure only these 3 options are selected and then click install.
4. [Download](https://github.com/emkaix/voicemeeter-themes-mod/archive/refs/heads/master.zip) and unzip or clone the repository.
5. In the start menu, search and run `Developer PowerShell for VS 2022` and navigate to the downloaded folder using `cd`.
6. Run **one** of the following commands, depending on the Voicemeeter version you are using:
    - Voicemeeter Default 32bit:
      ```pwsh
      cmake -S . -B out -G "Visual Studio 17 2022" -A Win32 -DDEFINE_FLAVOR_DEFAULT=1
      ```
    - Voicemeeter Default 64bit:
      ```pwsh
      cmake -S . -B out -G "Visual Studio 17 2022" -A x64 -DDEFINE_FLAVOR_DEFAULT=1
      ```
    - Voicemeeter Banana 32bit:
      ```pwsh
      cmake -S . -B out -G "Visual Studio 17 2022" -A Win32 -DDEFINE_FLAVOR_BANANA=1
      ```
    - Voicemeeter Banana 64bit:
      ```pwsh
      cmake -S . -B out -G "Visual Studio 17 2022" -A x64 -DDEFINE_FLAVOR_BANANA=1
      ```
    - Voicemeeter Potato 32bit:
      ```pwsh
      cmake -S . -B out -G "Visual Studio 17 2022" -A Win32 -DDEFINE_FLAVOR_POTATO=1
      ```
    - Voicemeeter Potato 64bit:
      ```pwsh
      cmake -S . -B out -G "Visual Studio 17 2022" -A x64 -DDEFINE_FLAVOR_POTATO=1
      ```
7. Run
   ```pwsh
   cmake --build out --config Release
   ```
8. The build artifacts are now located in `out/Release`.

## Frequently Asked Questions

### I have problems using this, can I go to the Voicemeeter Discord / forum and ask for help there?

No, this is a completely unofficial mod that is not affiliated with Voicemeeter or VB-Audio Software in any shape or form, the moderators won't be able to help you. If you find a bug related to this mod, open an issue here on Github. 

### How does it work?

The launcher injects a DLL into the Voicemeeter process that hooks (= intercepts) some Windows API functions responsible for drawing on the application surface. This includes: [CreatePen](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createpen), [CreateBrushIndirect](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createbrushindirect) and [SetTextColor](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-settextcolor), among others. It also hooks a specific function in order to swap out the background bitmap.
Hooking is done using the Microsoft Detours library: https://github.com/microsoft/Detours/wiki

### Does it modify the Voicemeeter executable on disk?

No, the original files are completely untouched. The vmtheme launcher simply launches the Voicemeeter process and modifies the loaded image in RAM (specifically it modifies the IAT so that it loads the vmtheme.dll, see [https://github.com/microsoft/Detours/wiki/OverviewHelpers](https://github.com/microsoft/Detours/wiki/OverviewHelpers) for more information). If you face any problems, simply launch Voicemeeter without the vmtheme launcher as you would normally.

### I have Voicemeeter configured to run on Windows startup, how can I autostart Voicemeeter with the theme applied?

1. Untick `Run on Windows Startup` in the Voicemeeter menu
2. Create a shortcut to the vmtheme launcher in `C:\Users\<USER>\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\Startup`

### Does using this mod alone give me a themed Voicemeeter?

No, this mod only enables theme support. You also need to download a supported theme separately, see [Supported themes](#supported-themes).
         
### How do I make my own theme?

TODO

### I don't want to build from source, why are there no prebuilt binaries to download?

Due to the nature of the mod, specifically function hooking and DLL injection, some antivirus solutions will flag the mod binaries as potential malware. Since there is
no easy way for me to proof to you that prebuilt binaries are *actually* built from the source you see here on Github, it's in your best interest to make sure the source code is sound and then compile it yourself.
I don't recommend using this project (or any similar project for that matter) if you are unsure about this and/or can't verify the source code yourself.


## Dependencies

- Microsoft Detours [https://github.com/microsoft/Detours](https://github.com/microsoft/Detours)
  - For hooking Windows API and custom functions.
- nlohmann json [https://github.com/nlohmann/json](https://github.com/nlohmann/json)
  - For parsing the json config files.

The dependencies are pulled and built from source in the build step via the CMakeLists.txt file.
