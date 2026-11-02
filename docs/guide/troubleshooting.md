# Troubleshooting

## Preparing the data

**The build says the hotkeys cannot be read.** The CD is protected by SafeDisc,
whose `EMPIRES2.EXE` is encrypted. The game plays without hotkeys; everything
else is prepared as usual.

**The data will not prepare.** Check that `original_game_1999/` holds the CD's
`GAME/`, `SYSTEM/` and `GOODIES/` folders, and that Python 3 and `cabextract`
are installed. The CD must be the original *The Age of Kings*, version 1.0, not
The Conquerors or a later edition.

## Phones, tablets and the Switch

**The app asks for the data again after an update.** The new version plays
from newer data. Make the folder again with that release's source code and
choose it; saved games are kept.

**"That folder is not the game's data."** Choose the `OpenAoK-data` folder
itself, the one holding `manifest.txt` and `version.txt`.

**"Game data of version N."** The folder was made by another release's source
code. Make it with the same release as the app.

**A file is named as damaged on the Switch.** Copy the folder to the card again,
through another card reader or Hekate's USB mass storage: some USB card readers
damage what they write to large cards.

**The Switch says to start from a game.** OpenAoK needs all of the console's
memory: hold R while starting any game to open the Homebrew Menu, rather than
starting it from the Album.

## iPhone and iPad

**The app no longer opens after a week.** An app signed with a free Apple ID
runs for seven days. Sign and install it again; installing over it keeps the
data and saved games.

**Xcode marks the device "mismatched platform".** The simulator's project is
open. Run `ios/build.sh --device` and open `ios/build/OpenAoK.xcodeproj`.

**Xcode says the device's iOS is unsupported.** The device runs a newer iOS
than the Xcode's SDK: update Xcode.

## Network games

**The game stops with a desync message.** Two machines disagreed about the
world. Check that every player has the same version of OpenAoK.

**A game on the internet cannot be found.** Over TCP/IP, type the host's address
in Show Games; the host's router must let UDP port 27015 and the game's port
through.

## Still stuck

Open an [issue](https://github.com/Zaydme/openaok/issues) with your platform,
the version, and what the game printed: on a desktop the terminal, on Android
`adb logcat -s OpenAoK`, on the Switch `switch/openaok/openaok.log`.
