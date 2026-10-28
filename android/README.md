# Android build

OpenAoK as an APK for arm64 phones running Android 10 (API 29) or later, built
against Android 16 (API 36).

## Tools

- JDK 21. Gradle runs on it whichever JDK `java` is
  (`gradle/gradle-daemon-jvm.properties`), finding it among those installed.
- The Android SDK with `platforms;android-36`, `build-tools;35.0.0`,
  `ndk;28.2.13676358` and `cmake;3.31.6`, and `android/local.properties`
  naming it: `sdk.dir=/path/to/Android/sdk`.
- Python 3, CMake, a C++ compiler and `cabextract` on the building machine, to
  prepare the game data (see the main README).
- SDL 2 and FreeType sources: `./fetch_deps.sh` downloads them into `deps/`.

## Build

```sh
cd android
./fetch_deps.sh
./gradlew assembleRelease
```

The build prepares the game data from `original_game_1999/` if it has not been
prepared yet, and puts it in the APK with the CD files the engine reads and the
building machine's Arial. The APK is `app/build/outputs/apk/release/app-release.apk`,
signed with the debug key; install it with `adb install -r` or by opening it on
the phone. On first launch the app copies the game data to its own storage.

`adb logcat -s OpenAoK` shows what the engine prints.

## Controls

| Gesture | Does |
|---|---|
| Tap | Left click |
| Hold still for a moment | Right click (the phone vibrates) |
| Drag one finger | Left drag: box-select units, lay a wall |
| Drag two fingers | Scroll the map |
| Back | Escape |

While a building is being placed, the finger slides it and lifting places it.
A mouse plugged in works as on a desktop, edge scrolling included. The keyboard
comes up whenever there is a line to type.
