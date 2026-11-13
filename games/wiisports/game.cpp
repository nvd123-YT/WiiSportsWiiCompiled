#include "wp/game.h"

namespace wp::game {

namespace {

constexpr Description kDescription = {
    "Wii Sports",
    "wiisports",
    "games/wiisports/extracted",
    "games/wiisports/nand",
    "games/wiisports/settings.ini",
    "games/wiisports/textures",
    "",
    nullptr,
    0,
    "",
    "",
};

}

const Description& description() {
    return kDescription;
}

}
