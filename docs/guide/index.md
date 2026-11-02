# Introduction

OpenAoK is a new engine, written from scratch, that plays *Age of Empires II:
The Age of Kings* (1999) from your own copy of its CD. It plays the same units,
maps, art, sounds, music, movies and campaigns as the original, on a desktop, a
phone or tablet, and the Nintendo Switch.

The engine contains no code or data from the original game. It reads the
original's data files from your copy and implements their rules: every unit's
numbers, every technology and civilisation, the random map scripts and the
computer players' scripts all come from the 1999 data, and the interface is
drawn from the original art in its own fonts and words.

## What you need

- **Your own copy of the original CD**, *The Age of Kings*, version 1.0. The
  Conquerors expansion and the HD and Definitive editions use different data
  and do not work. See [Your game files](/guide/game-files).
- **A computer** to prepare the game's data from the CD, once, with Python 3.
  On a desktop the build does this for you; for a phone, a tablet or the Switch
  you make a data folder on a computer and copy it over.
- **A build of OpenAoK** for your platform, from the
  [releases](https://github.com/Zaydme/openaok/releases) or built from source.

## Where it runs

| Platform | What you download | Guide |
|---|---|---|
| Windows 10 and 11 (64-bit) | `OpenAoK-windows-<version>.zip` | [Windows](/guide/windows) |
| macOS and Linux | built from source | [macOS and Linux](/guide/desktop) |
| Android 10 or later (arm64) | `OpenAoK-<version>.apk` | [Android](/guide/android) |
| iPhone and iPad, iOS 15 or later | `OpenAoK-<version>.ipa`, or built with Xcode | [iPhone and iPad](/guide/ios) |
| Nintendo Switch (homebrew) | `OpenAoK-switch-<version>.zip` | [Nintendo Switch](/guide/switch) |

None of the downloads hold any of the game's data. On a desktop the build and
the Windows folder prepare it next to the game; on the other platforms the data
is a folder you make yourself, which stays your own copy: keep it to your own
devices.

## Then

- [Controls](/guide/controls): the mouse and keyboard, touch, and the Switch's
  controller.
- [Options](/guide/options): what each option does, including the interface
  sizes and the production queue.
- [Improvements on the original](/guide/improvements): what OpenAoK adds, such
  as zoom and larger populations.
- [Multiplayer](/guide/multiplayer): network games on the local network or by
  address.
- [How the engine works](/engine/), for the curious.
