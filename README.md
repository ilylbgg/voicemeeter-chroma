# Voicemeeter Themes Mod

Enables theming support for Voicemeeter.

- [Disclaimer](#disclaimer)
- [Getting started](#getting-started)
- [FAQ](#frequently-asked-questions)

## Disclaimer

**This project is an unofficial, third-party, independent modification of Voicemeeter. It is not affiliated with, endorsed by, or in any way officially connected to VB-Audio Software or any of its subsidiaries or affiliates. All trademarks, service marks, and copyrights related to Voicemeeter are the property of VB-Audio Software.**


**If you have any problems related to or caused by this mod, you will not get any support whatsoever on the Voicemeeter forums or the Voicemeeter Discord.**


## Getting started

1. [Build the project from source](#build).
2. Copy `vmtheme64.dll`/`vmtheme32.dll` into `C:\Program Files (x86)\VB\Voicemeeter\`
3. Choose a supported theme and place it in `C:\Users\<USER>\Documents\Voicemeeter\themes\`
4. Set the active theme in `theme.json`.
5. Launch Voicemeeter with the launcher. The name depends on your build target, for Banana x64 the name is `vmtheme64-banana.exe`


## Build

Install the following toolchain if you don't have it installed already (older versions of the toolchain may work as well). After building, you can uninstall everything, if you want.

1. Download and run [Build Tools for Visual Studio 2022](https://visualstudio.microsoft.com/downloads/) (scroll down and expand "Tools for Visual Studio").
2. Under "Individual components" select the following:
   1. Select `MSVC v143 - VS 2022 C++ x64/x86 build tools (Latest)`
   2. Select `Windows 11 SDK (10.0.26100.0)`
   3. Click install.
3. Download and install [Cmake Windows x64 Installer](https://cmake.org/download/)
4. Download or clone the repository and open the folder.
5. Shift + right click on empty space inside the folder and select "Open PowerShell window here". Alternatively, open PowerShell and navigate to the folder.
6. Run **one** of the following commands, depending on the Voicemeeter version you are using:
    - VoiceMeeter Default 32bit: 
      - `cmake -S . -B out -G "Visual Studio 17 2022" -A Win32 -DDEFINE_FLAVOR_DEFAULT=1`
    - VoiceMeeter Default 64bit:
      - `cmake -S . -B out -G "Visual Studio 17 2022" -A x64 -DDEFINE_FLAVOR_DEFAULT=1`
    - VoiceMeeter Banana 32bit:
      - `cmake -S . -B out -G "Visual Studio 17 2022" -A Win32 -DDEFINE_FLAVOR_BANANA=1`
    - VoiceMeeter Banana 64bit:
      - `cmake -S . -B out -G "Visual Studio 17 2022" -A x64 -DDEFINE_FLAVOR_BANANA=1`
    - VoiceMeeter Potato 32bit:
      - `cmake -S . -B out -G "Visual Studio 17 2022" -A Win32 -DDEFINE_FLAVOR_POTATO=1`
    - VoiceMeeter Potato 64bit:
      - `cmake -S . -B out -G "Visual Studio 17 2022" -A x64 -DDEFINE_FLAVOR_POTATO=1`
7. Run `cmake --build out --config Release`.
8. The build artifacts are now located in `out/Release`.

## Frequently Asked Questions

### I have problems using this, can I go to the Voicemeeter Discord / forum and ask for help there?

No, this is a completely unofficial mod that is not affiliated with Voicemeeter or VB-Audio Software in any shape or form, the moderators won't be able to help you. If you find a bug related to this mod, open an issue here on Github. 

### How does it work?

The default background image and UI colors are replaced at runtime. The launcher injects a DLL into the Voicemeeter process that hooks (= intercepts) some Windows API functions responsible for drawing on the application surface. It also hooks a specific function in order to swap out the background bitmap.
Hooking is done using the Microsoft Detours library: https://github.com/microsoft/Detours

### Does it modify the Voicemeeter executable on disk?

No, the original files are completely untouched. The vmtheme launcher simply launches the Voicemeeter process and modifies the loaded image in RAM (specifically it modifies the IAT so that it loads the vmtheme.dll, see [https://github.com/microsoft/Detours/wiki/OverviewHelpers](https://github.com/microsoft/Detours/wiki/OverviewHelpers) for more information). If you face any problems, simply launch Voicemeeter without the vmtheme launcher as you would normally.

### I have Voicemeeter configured to run on Windows startup, how can I auto-launch it with the theme?

1. Untick `Run on Windows Startup` in the Voicemeeter menu
2. Create a shortcut to the vmtheme launcher in `C:\Users\<USER>\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\Startup`

### Does using this mod alone give me a themed Voicemeeter?

No, this mod only enables theme support. You also need to download a supported theme separately, for example this one: https://github.com/emkaix/voicemeeter-theme-catppuccin-macchiato

### How do I make my own theme?

TODO

### I don't want to build from source, why are there no prebuilt binaries to download?

Due to the nature of the mod, specifically function hooking and DLL injection, some antivirus solutions will flag the mod binaries as potential malware. Since there is
no easy way for me to proof to you that the offered prebuilt binaries are *actually* built from the source you see here on Github, it's in your best interest to make sure the source code is sound and then compile it yourself.


## Dependencies

- Microsoft Detours [https://github.com/microsoft/Detours](https://github.com/microsoft/Detours)
  - For hooking Windows API and custom functions.
- nlohmann json [https://github.com/nlohmann/json](https://github.com/nlohmann/json)
  - For parsing the json config files.

The dependencies are pulled and built from source in the build step via the CMakeLists.txt file.
