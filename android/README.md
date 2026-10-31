# Android

OpenAoK as an APK for arm64 phones and tablets running Android 10 (API 29) or
later, built against Android 16 (API 36). The APK holds none of the game: on
its first start it asks for a folder of the game's data, made on a computer
from your own CD, and copies it into its own storage.

## Playing

**1. Install the APK.** Download `OpenAoK-<version>.apk` from the
[releases](https://github.com/Zaydme/openaok/releases) and open it on the
phone; Android asks to allow installing from the browser or file manager once.
An APK you built yourself is signed with another key than a released one, and
Android installs one over the other only after the first is uninstalled.

**2. Make the game's data.** On a computer, download the same release's source
code (or clone the repository) and copy your CD into its `original_game_1999/`,
as the main README says. Preparing the data needs Python 3, cabextract, CMake
and a C++ compiler:

```sh
# macOS (with the Xcode command-line tools)
brew install cmake cabextract

# Debian / Ubuntu
sudo apt install build-essential cmake python3 zlib1g-dev cabextract
```

Then:

```sh
python3 android/package.py
```

It prepares the game's data as `build.sh` does and writes
`android/build/OpenAoK-data/`: about 350 MB, with `manifest.txt` (every file's
checksum, size and path) and `version.txt` (which version of the data it is).
`--out DIR` writes `DIR/OpenAoK-data/` instead.

**3. Copy the folder to the phone.** Anywhere the phone's file picker can
reach: over USB (choose File transfer on the phone and drag `OpenAoK-data` into
its Download folder), with `adb push android/build/OpenAoK-data /sdcard/Download/`,
or on a memory card.

**4. Choose it.** Open OpenAoK. It says it needs the game's data; tap Choose
folder, open Download, then `OpenAoK-data` itself (Android does not let an app
take Download as a whole), and tap Use this folder, then Allow. The app copies
the data in, checking every file against the manifest, and starts the game.
The copy on the phone can then be deleted.

After an update, if the new version plays from newer data, the app says so on
its first start: make the folder again with that release's source code and
choose it, or play on with the data already there.

## Controls

| Gesture | Does |
|---|---|
| Tap | Left click |
| Hold still for a moment | Right click (the phone vibrates) |
| Drag one finger | Left drag: box-select units, lay a wall |
| Drag two fingers | Scroll the map |
| Pinch | Zoom in and out |
| Back | Escape |

While a building is being placed, the finger slides it and lifting places it.
A mouse plugged in works as on a desktop, edge scrolling included. The keyboard
comes up whenever there is a line to type.

## Building the APK

Tools:

- JDK 21. Gradle runs on it whichever JDK `java` is
  (`gradle/gradle-daemon-jvm.properties`), finding it among those installed.
- The Android SDK with `platforms;android-36`, `build-tools;35.0.0`,
  `ndk;28.2.13676358` and `cmake;3.31.6`, and `android/local.properties`
  naming it (`sdk.dir=/path/to/Android/sdk`), or `ANDROID_HOME` set.
- SDL 2 and FreeType sources: `./fetch_deps.sh` downloads them into `deps/`.

```sh
cd android
./fetch_deps.sh
./gradlew assembleRelease
```

The APK is `app/build/outputs/apk/release/app-release.apk`. It needs no game
files to build. Install it with `adb install -r` or by opening it on the phone.
`adb logcat -s OpenAoK` shows what the engine prints.

It is signed with the debug key unless a release key is given: by
`OPENAOK_KEYSTORE`, `OPENAOK_KEYSTORE_PASSWORD`, `OPENAOK_KEY_ALIAS` and
`OPENAOK_KEY_PASSWORD` in the environment, or by `openaok.keystore`,
`openaok.keystorePassword`, `openaok.keyAlias` and `openaok.keyPassword` in
`~/.gradle/gradle.properties`. `OPENAOK_VERSION` names the version shown in
Android's settings.

GitHub builds the APK on every push (`.github/workflows/android.yml`) and adds
it to the release of a version tag, signed with the project's release key.
