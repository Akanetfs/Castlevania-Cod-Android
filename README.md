# Castlevania: Curse of Darkness — Android Port

An unofficial Android port of *Castlevania: Curse of Darkness*, based on the original Xbox release.

This project does not include any game assets and nor will it include any type of file related to the original game

The project uses ahead-of-time recompilation of the game's x86 code to C++, with an Android native host. Rendering is handled through Vulkan; platform input, audio and window integration use SDL3.

## Features

I’ve added a couple of extra features, in addition to touch controls and game controllers. You can access these options by holding down the Select button anywhere in the game (in a menu or during gameplay).

##Render resolution

- You can adjust the resolution within the game itself, ranging from 1170x540 (50%) up to a maximum of 150% (3510x1620).

## Controls

You can change the position, opacity, and size of the touch buttons from within the game itself.

## Controller button mapping

From the launcher, you can map your controller buttons however you prefer.

## Additional save file

This save file is useful in case you uninstall the app; when you decide to reinstall it, you can load your game from the `storage/Castlevania` folder. (Note that if you uninstall or update the app, all game data will be lost.)

## Game information

You can display both the FPS and resolution within the game itself to monitor performance.

Install the following tools:

- Visual Studio 2022 with **Desktop development with C++**.
- Android Studio and Android SDK Platform 34.
- Android NDK `28.2.13676358`.
- CMake `3.31.6` from the Android SDK.
- Java 17 or newer.
- Git for Windows.
- Python 3, used by the helper script when extracting `default.xbe` from an ISO.

The project fetches SDL3 from its pinned tag during the Gradle build. CMake also fetches its configured third-party dependencies.

## Build

1. Clone this repository:

   ```bat
   git clone https://github.com/Akanetfs/Castlevania-Cod-Android.git
   cd Castlevania-Cod-Android
   ```

2. Place your legally obtained game files locally (these files are ignored by Git):

   - Put the European `default.xbe` at `assets/default.xbe`, or
   - Put your disc image (`.iso`) in `assets/` or `iso/`. The build script can extract `default.xbe` from it.

3. Install the required tools listed above. Make sure Android SDK, NDK, Java and Git are available to the build environment.

4. Run:

   ```bat
   build.bat
   ```

   The script prepares the recompiler, generates the game-specific C++ sources locally, and invokes Gradle to build the Android release APK.

5. The APK output is:

   ```text
   app\build\outputs\apk\release\app-release.apk
   ```

6. Install it on a connected Android device:

   ```bat
   adb install -r app\build\outputs\apk\release\app-release.apk
   ```

The first native build can take a substantial amount of time. Build output, generated code and game files are intentionally excluded from version control.

## Installing and providing game data

Install the APK, then follow the in-app instructions for placing the required game data on the device. The game data must come from your own legally acquired copy.

The application uses the package ID `com.codandroid.recomp`. Android application data and save locations are managed by the app; avoid deleting application data if you need to preserve local saves.

## Credits and third-party software

This project builds on work and libraries from:

- [XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp)
- [SDL](https://github.com/libsdl-org/SDL)
- [glslang](https://github.com/KhronosGroup/glslang)
- [toml++](https://github.com/marzer/tomlplusplus)
- [Dear ImGui](https://github.com/ocornut/imgui)
- [Black for Android](https://github.com/RanggaCasper/Black-Android)

Their respective authors retain ownership of their work. Third-party components remain subject to their own licenses; see their upstream repositories and included license notices.

*Castlevania* and related characters, names and assets are trademarks and/or copyrighted works of their respective owners. This is an unofficial fan project and is not affiliated with, sponsored by, or endorsed by Konami. No original game content is distributed by this repository.
