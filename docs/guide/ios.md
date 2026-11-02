# iPhone and iPad

OpenAoK runs on iPhones and iPads with iOS 15 or later. The app holds none of
the game's data: the data is a folder made on a computer from your own CD and
copied to the device, which the app copies in on its first start.

## 1. Make the game's data

With the source code and your CD in `original_game_1999/`, on a Mac with the
same tools as for [macOS](/guide/desktop):

```sh
python3 ios/package.py
```

It writes `ios/data/OpenAoK-data/`, about 350 MB, with `manifest.txt` and
`version.txt`.

## 2. Install the app

Either way needs an Apple ID to sign the app; a free one works.

**With Xcode** (15 or later):

```sh
ios/build.sh --device
```

Open `ios/build/OpenAoK.xcodeproj`, choose your team under Signing &
Capabilities and your iPhone or iPad as the destination, and Run.

**Sideloaded**: a release's `OpenAoK-<version>.ipa` is unsigned, for a
sideloading tool such as AltStore or Sideloadly to sign with your Apple ID and
install. `ios/build.sh --device --ipa` makes the same `.ipa` from source.

::: tip Seven days
An app signed with a free Apple ID runs for seven days; then it has to be
signed and installed again. Installing over the app keeps its data and saved
games. A paid developer team's signature lasts a year.
:::

## 3. Copy the data and choose it

Copy `OpenAoK-data` to the device: with AirDrop, the Files app, or Finder's
File Sharing (the app shows up in Finder's Files tab with the device
connected). Open OpenAoK: it says it needs the game's data and opens the
document picker. Choose the `OpenAoK-data` folder itself; the app copies the
data in, checking every file against the manifest, and starts the game. The
copy on the device can then be deleted.

A folder dragged into the app's own Documents through Finder is found there on
the next start, without the picker.

The game's data, saved games and the profile are in the app's Documents
folder, under On My iPhone or iPad, OpenAoK in the Files app.

The [controls](/guide/controls#touch) are the touch screen's; a hardware
keyboard and a mouse or trackpad work as on a desktop.
