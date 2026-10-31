# Windows

OpenAoK for 64-bit Windows 10 and 11, played from your own copy of the
original CD. The download holds none of the game: the game's data is prepared
from the CD's files the first time it starts.

## Play

1. Unzip `OpenAoK-windows-<version>.zip` from the
   [releases](https://github.com/Zaydme/openaok/releases) anywhere you like.
2. Copy everything on the CD (`GAME`, `SYSTEM`, `GOODIES` and the rest) into
   the `OpenAoK\original_game_1999` folder.
3. Install [Python 3](https://www.python.org/downloads/windows/). It is needed
   once, to prepare the data.
4. Double-click `OpenAoK.bat`. The first time it prepares the data from the
   CD's files, which takes a few minutes, and then the game starts. After
   that it starts at once.

The folder is laid out as the source tree is: the game, `build\OpenAoK.exe`,
reads `..\assets` and `..\original_game_1999`, so it can also be started from
`build\` directly once the data is prepared. Command-line options are those of
the main README.

## Build

On Debian or Ubuntu, with MinGW-w64:

```sh
sudo apt install mingw-w64 cmake ninja-build curl xz-utils
windows/build.sh
```

downloads SDL2's MinGW development files, FreeType and win-iconv (checked
against their SHA-256), builds `OpenAoK.exe` and the data tools' dat dumper,
and lays out `windows/build/OpenAoK`, the folder the release zips. The
executable needs only `SDL2.dll` beside it. On a Mac the same runs in a Debian
container (`brew install colima docker && colima start`).

GitHub Actions builds it on every push (`.github/workflows/windows.yml`), and
a version tag (`v*`) adds `OpenAoK-windows-<version>.zip` to that version's
release.

The simulation is the same fixed-point code on every platform: a Windows build
plays the self-check's determinism game to the same hash as the others, so it
can join their network games.
