# iOS

OpenAoK as an app for iPhones and iPads running iOS 15 or later, built with
Xcode for `arm64`. The app holds none of the game: on its first start it asks
for a folder of the game's data, made on a computer from your own CD, and
copies it into its Documents folder.

The engine's own sources are unchanged. `ios/platform/PlatformiOS.mm` is the
platform layer: the document picker, the checked copy into the sandbox, and the
player's files' folder. Everything else -- SDL's `SDL_main` entry point, the
touch controls -- is the same code the desktop and Android builds use.

## What you need

- A Mac with Xcode 15 or later, opened once so its licence is accepted.
- CMake 3.20 or later (`brew install cmake`).
- Python 3 and `cabextract` to prepare the game's data, as for the desktop
  build (`brew install cabextract`).
- An Apple ID for installing on a device: a free one works through Xcode, and
  AltStore with a free one too. A paid team is needed only for App Store
  distribution.

## 1. Fetch SDL2 and FreeType

```sh
cd ios
./fetch_deps.sh
```

They are built from source into the app; nothing is installed system-wide.
`deps/` is not kept in git.

## 2. Make the game's data

On the Mac, with the CD copied into `original_game_1999/` in the repository (as
the main README says):

```sh
python3 ios/package.py
```

It writes `ios/data/OpenAoK-data/` (not kept in git): about 350 MB of the
game's data with `manifest.txt` (every file's checksum, size and path) and
`version.txt` (which version of the data it is). `--out DIR` writes
`DIR/OpenAoK-data/` instead.

## 3. Build

```sh
ios/build.sh               # simulator, no signing needed
ios/build.sh --project     # only write the Xcode project
```

It writes `ios/build-sim/OpenAoK.xcodeproj` and builds
`ios/build-sim/Release-iphonesimulator/OpenAoK.app`.

For a device, whose project is `ios/build/OpenAoK.xcodeproj`:

```sh
ios/build.sh --device
```

and the app is `ios/build/Release-iphoneos/OpenAoK.app`. Without
`OPENAOK_TEAM_ID` set it is left unsigned, for AltStore to sign.

Both are ordinary Xcode projects: open one, choose the OpenAoK scheme and a
destination, and Run. To build from Xcode with signing, select the project's
target, Signing & Capabilities, and choose your team; or set the team for the
command line too:

```sh
OPENAOK_TEAM_ID=ABCDE12345 ios/build.sh --device
```

The project can also be written by hand:

```sh
cmake -G Xcode -S ios -B ios/build -DCMAKE_SYSTEM_NAME=iOS
```

`-DCMAKE_OSX_SYSROOT=iphonesimulator` configures a simulator build instead, and
`-DCMAKE_OSX_DEPLOYMENT_TARGET` picks another oldest iOS.

## 4. Run in the simulator

From Xcode: open `ios/build-sim/OpenAoK.xcodeproj`, choose an iPhone or iPad
simulator and Run.

From the terminal:

```sh
open -a Simulator
xcrun simctl install booted ios/build-sim/Release-iphonesimulator/OpenAoK.app
xcrun simctl launch booted org.openaok.game
```

The simulator has no Files app to choose the folder from, so put the game's
data in the app's own folder and launch it:

```sh
xcrun simctl install booted ios/build-sim/Release-iphonesimulator/OpenAoK.app
DOCS="$(xcrun simctl get_app_container booted org.openaok.game data)/Documents"
mkdir -p "$DOCS"
cp -R ios/data/OpenAoK-data "$DOCS/"
xcrun simctl launch booted org.openaok.game
```

The app finds it there on start -- its `OpenAoK-data` folder, holding
`version.txt` and `assets/extracted_data/rules.json` -- and goes straight into
the game.

## 5. Install on an iPhone or iPad with Xcode

1. `ios/build.sh --device`, or open `ios/build/OpenAoK.xcodeproj` and set your
   team under Signing & Capabilities.
2. Connect the device, trust the Mac on it, and choose it as the destination.
3. Run. The first launch asks for the game's data.

A free Apple ID's signing lasts seven days; the app then has to be installed
again from Xcode. A paid team's lasts a year.

## 6. Sideload with AltStore

AltStore installs an unsigned `.ipa` and signs it with your Apple ID. Make one
from the device build:

```sh
ios/build.sh --device
cd ios/build/Release-iphoneos
rm -rf Payload OpenAoK.ipa
mkdir Payload
cp -R OpenAoK.app Payload/
zip -qry OpenAoK.ipa Payload
```

Then, with AltStore on the device (and AltServer running on the Mac, the device
on the same network): My Apps, `+`, and choose `OpenAoK.ipa` -- from Files if it
was AirDropped or copied over, or through AltServer. AltStore signs it on the
device; a free Apple ID has to refresh it every seven days.

## 7. The game's data on the device

Copy `ios/data/OpenAoK-data` to the device in any way it can be reached:
AirDrop, the Files app, or Finder over USB with the device's Files sharing
(the app shows up in Finder's File Sharing list, and its Documents folder holds
it after the first launch).

Open OpenAoK. It says it needs the game's data and opens the system's document
picker: choose the `OpenAoK-data` folder itself (open the folder it is in, then
tap it, then Open). The app copies the data in, checking every file against
`manifest.txt`, and starts the game; the copy on the device can then be
deleted.

The data lives in the app's `Documents/OpenAoK-data/`, saved games and the
profile in `Documents/SaveGame/`, `Documents/Scenario/` and
`Documents/Profile/` beside it, all visible in the Files app under On My
iPhone/iPad, OpenAoK.

After an update that plays from newer data, the app asks for the folder again
on its first start. Make the folder with that release's source code and choose
it; saved games are kept.

## Controls

| Gesture | Does |
|---|---|
| Tap | Left click |
| Hold still for a moment | Right click |
| Drag one finger | Left drag: box-select units, lay a wall |
| Drag two fingers | Scroll the map |
| Pinch | Zoom in and out |

While a building is being placed, the finger slides it and lifting places it.
A hardware keyboard and a mouse or trackpad work as on a desktop, including
Escape for the game's menus; the software keyboard comes up whenever there is
a line to type.

## How it is built

- `ios/CMakeLists.txt` builds the engine's sources from `src/` and the
  platform files from `ios/platform/` into `OpenAoK.app`, against SDL2 and
  FreeType built from `ios/deps`. SDL2 is static, and its uikit `main` starts
  the app and calls the engine's `main` as `SDL_main` -- the standard SDL2
  entry point.
- `ios/Info.plist` keeps the app in landscape, hides the status bar, and makes
  Documents visible to the Files app and Finder.
- `ios/platform/PlatformiOS.mm` implements `HAL::iOS`, which
  `src/hal/Platform.cpp` calls on iOS: it checks for the data, asks for it with
  `UIDocumentPickerViewController`, copies it with a CRC-32 check, enters
  `OpenAoK-data/build/` as the working directory (the engine names its files
  from `build/`, as `../assets/...`), and gives the player's files their
  folders under Documents.
- `ios/package.py` lays the data out with `tools/data/package_data.py`, the
  same layout the Switch and Android builds use.
- Touch controls are `src/hal/Touch.cpp` unchanged; `src/hal/Pointer.cpp`
  sends SDL the mouse it makes from the fingers, as its Switch port does.

Nothing of the simulation, the fixed-point maths or the determinism checks is
iOS-specific; `bash tools/check_determinism.sh` still passes on the Mac.

## Troubleshooting

- **`Could not find a package configuration file ... SDL2`** -- the project
  was configured outside `ios/CMakeLists.txt`. Use `ios/build.sh`, or pass
  `-S ios`.
- **`no such file ... deps/SDL2-2.32.10`** -- run `ios/fetch_deps.sh`.
- **The build stops on signing** -- choose a team in Xcode's Signing &
  Capabilities, or build unsigned (`ios/build.sh --device`) for AltStore.
- **"That folder is not the game's data"** -- the picked folder must be the
  `OpenAoK-data` folder itself, the one holding `manifest.txt` and
  `version.txt`, made by `ios/package.py`.
- **"game data of version N"** -- the folder was made by another release's
  source code. Run `python3 ios/package.py` with this release's.
- **The game starts and stops at "Failed to open graphics.drs"** -- the copy in
  Documents is incomplete or of the wrong version; choose the folder again.
- **`ios/build-sim` and `ios/build` are stale after changing CMake options** --
  a different sysroot or deployment target needs its own build folder; delete
  the one in question and configure again.
