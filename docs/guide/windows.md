# Windows

OpenAoK runs on 64-bit Windows 10 and 11. The download holds none of the
game's data: the data is prepared from your own CD the first time it starts.

## Install

1. Unzip `OpenAoK-windows-<version>.zip` from the
   [releases](https://github.com/Zaydme/openaok/releases) anywhere you like.
2. Copy everything on your CD (`GAME`, `SYSTEM`, `GOODIES` and the rest) into
   its `OpenAoK\original_game_1999` folder.
3. Install [Python 3](https://www.python.org/downloads/windows/). It is needed
   once, to prepare the data.
4. Double-click `OpenAoK.bat`. The first time it prepares the data from the
   CD's files, which takes a few minutes, and then the game starts. After that
   it starts at once.

## The folder

The folder is laid out as the source tree is: the game, `build\OpenAoK.exe`,
reads `..\assets` and `..\original_game_1999`, so once the data is prepared it
can also be started from `build\` directly, with any of the
[command-line options](/guide/command-line). The executable needs only
`SDL2.dll` beside it.

Building the Windows version yourself is in
[Building from source](/development/).
