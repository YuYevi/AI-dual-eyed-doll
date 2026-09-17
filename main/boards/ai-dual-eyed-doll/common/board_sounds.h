#ifndef AI_DUAL_EYED_DOLL_BOARD_SOUNDS_H
#define AI_DUAL_EYED_DOLL_BOARD_SOUNDS_H

#include <string_view>

namespace BoardSounds {

extern const char short_laugh_ogg_start[] asm("_binary_Short_laugh_ogg_start");
extern const char short_laugh_ogg_end[] asm("_binary_Short_laugh_ogg_end");
inline const std::string_view OGG_SHORT_LAUGH{
    short_laugh_ogg_start, static_cast<size_t>(short_laugh_ogg_end - short_laugh_ogg_start)};

extern const char tsundere_ogg_start[] asm("_binary_tsundere_ogg_start");
extern const char tsundere_ogg_end[] asm("_binary_tsundere_ogg_end");
inline const std::string_view OGG_TSUNDERE{
    tsundere_ogg_start, static_cast<size_t>(tsundere_ogg_end - tsundere_ogg_start)};

}  // namespace BoardSounds

#endif  // AI_DUAL_EYED_DOLL_BOARD_SOUNDS_H