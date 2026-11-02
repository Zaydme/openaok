# Android

OpenAoK runs on arm64 phones and tablets with Android 10 (API 29) or later.
The APK holds none of the game: on its first start it asks for a folder of the
game's data, made on a computer from your own CD, and copies it into its own
storage.

## 1. Install the APK

Download `OpenAoK-<version>.apk` from the
[releases](https://github.com/Zaydme/openaok/releases) and open it on the
phone; Android asks once to allow installing from the browser or file manager.

::: tip
An APK you built yourself is signed with another key than a released one.
Android installs one over the other only after the first is uninstalled.
:::

## 2. Make the game's data

On a computer, download the same release's source code (or clone the
repository) and copy your CD into its `original_game_1999/`. Preparing the
data needs Python 3, cabextract, CMake and a C++ compiler:

::: code-group

```sh [macOS]
# with the Xcode command-line tools
brew install cmake cabextract
```

```sh [Debian / Ubuntu]
sudo apt install build-essential cmake python3 zlib1g-dev cabextract
```

:::

Then:

```sh
python3 android/package.py
```

It prepares the game's data as `build.sh` does and writes
`android/build/OpenAoK-data/`: about 350 MB, with `manifest.txt` (every file's
checksum, size and path) and `version.txt` (which version of the data it is).
`--out DIR` writes `DIR/OpenAoK-data/` instead.

## 3. Copy it to the phone

Anywhere the phone's file picker can reach: over USB (choose File transfer on
the phone and drag `OpenAoK-data` into its Download folder), with
`adb push android/build/OpenAoK-data /sdcard/Download/`, or on a memory card.

## 4. Choose it

Open OpenAoK. It says it needs the game's data; tap Choose folder, open
Download, then `OpenAoK-data` itself (Android does not let an app take Download
as a whole), and tap Use this folder, then Allow. The app copies the data in,
checking every file against the manifest, and starts the game. The copy on the
phone can then be deleted.

After an update that plays from newer data, the app says so on its first
start: make the folder again with that release's source code and choose it, or
play on with the data already there.

The [controls](/guide/controls#touch) are the touch screen's: tap to click,
hold to right-click, two fingers to scroll and pinch to zoom.
