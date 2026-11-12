// What Black's game code gave the Android host and Castlevania has no counterpart for yet. Each one is the neutral
// answer, so the host links and runs; none of them touches the guest.
#include "common/options.h"
#include "game/weapons.h"

namespace game {
bool HasLanguages() { return false; }                 // the game picks its language itself (see PORTING.md)
bool LanguageAvailable(const std::string&) { return true; }  // must stay true: the settings menu loops until it is
bool HudShown() { return true; }                      // the touch controls stay visible
}  // namespace game

namespace weapons {
View Current() { return {}; }
void Give(int) {}
void FillAmmo() {}
}  // namespace weapons
