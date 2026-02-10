#include "game/game_world.h"

int main()
{
    fox::game_world gw({ 1024, 768, "FoxGame", false, 0 });
    //fox::game_world gw({ 1920, 1080, "FoxGame", false, 0 });
    gw.init();
    gw.run();
    return 0;
}
