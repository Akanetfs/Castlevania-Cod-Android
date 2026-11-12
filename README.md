# Castlevania: Curse of Darkness — Android Port

An unofficial Android port of *Castlevania: Curse of Darkness*, based on the original Xbox release.

The project uses ahead-of-time recompilation of the game's x86 code to C++, with an Android native host. Rendering is handled through Vulkan; platform input, audio and window integration use SDL3.

> **Work in progress:** this port is under active development. Features and compatibility may change.

> **Game files are not included.** You must provide the required data from your own legally acquired copy of the game. Do not upload game disc images, executable files, copyrighted assets, generated game code, or save data to this repository.

## Current development status

The Android port is being developed with:
- Touchscreen controls and external gamepad support.
- Audio integration for menus, gameplay and cinematics.
- Ongoing work on cinematic playback and compatibility.
- Vulkan-based rendering and Android-native integration.

Some areas may work while others still have bugs. Check the repository's Issues and Releases for the latest public status.

## Repository layout

```text
.
├── app/                  # Android application module (Java, resources, manifest)
├── config/               # Recompiler configuration
├── native/               # Android native host and platform integration
├── src/                  # Game hooks, audio, graphics and common code
├── tools/                # Recompiler helper tools
├── CMakeLists.txt        # Native build configuration
├── build.gradle          # Root Gradle configuration
├── build.bat             # Windows build/recompilation script
├── gradle.properties
├── gradlew
└── README.md
```

Some dependencies are fetched during configuration/build and are not committed as vendored source.

## Requirements

The documented build workflow targets Windows and Android **arm64-v8a**.

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

## Troubleshooting

- **Missing game/recompiled code:** confirm that `assets/default.xbe` or a supported ISO is present locally, then run `build.bat` again.
- **Missing Android toolchain:** verify the installed SDK Platform, NDK and CMake versions against the Requirements section.
- **SDL3 checkout errors:** check your Git installation and network access; Gradle fetches the pinned SDL3 source when it is not already present.
- **Build or runtime bugs:** include the full build output or relevant device log when opening an Issue. Do not attach copyrighted game files.

## Credits and third-party software

This project builds on work and libraries from:

- [XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp)
- [SDL](https://github.com/libsdl-org/SDL)
- [glslang](https://github.com/KhronosGroup/glslang)
- [toml++](https://github.com/marzer/tomlplusplus)
- [Dear ImGui](https://github.com/ocornut/imgui)

Their respective authors retain ownership of their work. Third-party components remain subject to their own licenses; see their upstream repositories and included license notices.

*Castlevania* and related characters, names and assets are trademarks and/or copyrighted works of their respective owners. This is an unofficial fan project and is not affiliated with, sponsored by, or endorsed by Konami. No original game content is distributed by this repository.
