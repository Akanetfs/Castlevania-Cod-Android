#pragma once
#include <atomic>

// the developer rows of the settings panel (src/common/menu.cpp) and the game thread that plays what they ask for
// (D3DDevice_Swap in src/d3d/d3d8.cpp, the game's own movie routine in src/game/hooks.cpp)
namespace dev {

inline std::atomic<int> g_movie_index{0};   // the movie the row picks, 0 to 23
inline std::atomic<int> g_play_movie{-1};   // set by the panel, taken by the game thread: the movie to play, -1 none

}  // namespace dev
