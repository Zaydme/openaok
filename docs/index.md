---
layout: home

hero:
  name: OpenAoK
  text: Age of Kings, on a new engine
  tagline: A free, open-source engine for Age of Empires II, The Age of Kings (1999). It plays the original game from your own CD, on Windows, macOS, Linux, Android, iPhone and iPad, and the Nintendo Switch.
  image:
    src: /images/key.svg
    alt: OpenAoK
  actions:
    - theme: brand
      text: Get started
      link: /guide/
    - theme: alt
      text: How the engine works
      link: /engine/
    - theme: alt
      text: Download
      link: https://github.com/Zaydme/openaok/releases

features:
  - title: Your copy, your data
    details: The repository holds no game files. You copy your CD into a folder, and the build prepares everything the engine needs from it.
  - title: The original, faithfully
    details: Every unit, technology, civilisation and map script comes from the 1999 data. The interface is drawn from the original art, laid out as the game laid it out, in its own fonts and words.
  - title: Everywhere
    details: The same engine plays on a desktop, on a phone or tablet with touch controls, and on the Switch with its controller or touch screen.
  - title: Deterministic to the bit
    details: The simulation runs on 32.32 fixed point at a fixed 60 Hz, so the same orders make the same world on every machine. Multiplayer and the self-check are built on it.
  - title: The original computer players
    details: The computer players run the CD's own AI scripts in an interpreter of their language, at every difficulty.
  - title: Written from scratch
    details: Modern C++20 on SDL 2, data-oriented on EnTT, under the MIT license. No code or data from the original game.
---

## What it plays

- **Random maps** from the original map scripts, at every map size.
- **The rules**: every unit's numbers, all 426 technologies, each civilisation's tech tree and bonuses, the four ages.
- **Economy and war**: gathering, farms, trade, the Market, tribute; attack and armour classes, missiles, formations and stances, monks and relics, siege, walls and gates, docks and the navy.
- **Campaigns and scenarios** with their triggers, narration and pictures, the scenario editor, saved and recorded games.
- **Victory** by conquest, Wonder or relics, Regicide and Death Match, diplomacy, chat, taunts and the cheat codes.
- **Multiplayer** in lockstep over TCP, games on the local network found by themselves.

The engine's workings, from how a sprite is decoded to how a computer player decides where to attack, are explained in [The engine](/engine/).

::: info Not affiliated with Microsoft
OpenAoK is a free, non-commercial fan project. It is not affiliated with, endorsed by or sponsored by Microsoft, and it contains no game assets: you need your own copy of the game to play. *Age of Empires II: The Age of Kings* © Microsoft Corporation. *Age of Empires* is a trademark of Microsoft Corporation.
:::
