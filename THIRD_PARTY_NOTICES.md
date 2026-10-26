# Third-party notices

OpenAoK's own code is under the MIT license (`LICENSE`). The following parts
are not, and keep their own licenses.

| Component | Where | License |
|---|---|---|
| Indeo Video 5 decoder, following FFmpeg's libavcodec (Copyright (c) 2009-2011 Maxim Poliakovski) | `src/hal/Indeo5.h`, `Indeo5.cpp`, `Indeo5Tables.h` | LGPL-2.1-or-later (`LICENSES/LGPL-2.1.txt`) |
| EnTT | `third_party/entt` | MIT (`third_party/entt/LICENSE`) |
| JSON for Modern C++ (nlohmann/json) | `third_party/nlohmann` | MIT |
| minimp3 | `third_party/minimp3` | CC0 1.0 (public domain) |
| stb_image | `src/hal/stb` | MIT or public domain (Unlicense) |
| genieutils, used only by the data tools | `third_party/genieutils` | LGPL-3.0 (`third_party/genieutils/COPYING`, with the GPL-3.0 it extends in `LICENSES/GPL-3.0.txt`) |
| miniz, pcrio, zstr (bundled with genieutils) | `third_party/genieutils/extern` | Unlicense; see `pcrio/COPYING`; MIT |
| SDL 2 Java glue, Android build | `android/app/src/main/java/org/libsdl` | zlib |
| SDL 2 and FreeType, fetched by `android/fetch_deps.sh` | `android/deps` (not in the repository) | zlib; FreeType License |

The OpenAoK logo's lettering is set in Cinzel and Cinzel Decorative by Natanael
Gama, licensed under the SIL Open Font License 1.1.

## Game data

No part of *Age of Empires II: The Age of Kings* is included in this
repository. The game's data, art, sounds, music, movies, text and fonts belong
to Microsoft and are read from the player's own copy of the game at build time
and run time.
