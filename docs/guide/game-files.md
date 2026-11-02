# Your game files

OpenAoK plays from **your own, legally obtained copy** of *Age of Empires II:
The Age of Kings*: the original 1999 CD, version 1.0. It does not work with The
Conquerors expansion or with the HD and Definitive editions, which use
different data.

## Copying the CD

Copy everything on the CD into a folder named `original_game_1999` at the top
of the OpenAoK folder (the repository on a desktop, the unzipped folder on
Windows). From a disc or a mounted image on macOS, for example:

```sh
cp -R /Volumes/AOE2/. original_game_1999/
```

The folder then holds `GAME/`, `SYSTEM/` and `GOODIES/`. That is all: the build
unpacks and converts what it needs.

## What is prepared from it

The first build, or the first start on Windows, prepares the game's data in
`assets/`:

- the CD's archives and files, unpacked from its cabinets (with `cabextract`,
  or on Windows its own tools);
- the rule tables of the dat file and the strings of LANGUAGE.DLL, converted to
  JSON;
- the game's palette, the terrain blending masks and the default hotkeys.

At run time the engine reads the graphics, sounds, map scripts and AI scripts
straight from the original archives. [The game's data](/engine/data) explains
each file and how it is read.

For a phone, a tablet or the Switch, a script on the computer lays the prepared
data out as one folder, about 350 MB, with a manifest of every file's checksum:
see the guide for your platform.

## Keep it to yourself

Everything taken from your copy stays on your machine and is ignored by git.
Please do not share it, or the builds you make from it: a desktop build
contains your game data. The Android APK, the iOS app and the Switch's
`OpenAoK.nro` hold none of it; the data folders made for them do.

::: warning SafeDisc
The default hotkeys are read from the CD's `GAME/EMPIRES2.EXE`. On a CD
protected by SafeDisc that file's code is encrypted, so they cannot be read:
the build says so and goes on, and the game plays without hotkeys.
:::
