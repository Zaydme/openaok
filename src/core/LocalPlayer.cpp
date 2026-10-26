#include "core/Types.h"

namespace Core {

namespace {

PlayerId g_local = PLAYER_LOCAL;

} // namespace

PlayerId LocalPlayer() { return g_local; }

void SetLocalPlayer(PlayerId player) { g_local = player; }

} // namespace Core
