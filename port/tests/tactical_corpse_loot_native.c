#include "gevr_tactical_loot.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void)
{
    /* Face down: the real magazine is below the torso. A hand above the back
     * must reach that same magazine even though it is outside 20 cm of it. */
    float front[3] = {0, 0, 0}, back[3] = {0, 20, 0}, hand[3] = {0, 35, 0};
    assert(gevrLootDistanceSquared(hand,front,back,0) > 400);
    assert(fabsf(gevrLootDistanceSquared(hand,front,back,1) - 225) < .001f);
    /* The back proxy is bounded: a distant hand cannot loot the corpse. */
    hand[0] = 30;
    assert(gevrLootDistanceSquared(hand,front,back,1) > 625);
    /* A hand near the chest remains reachable, including for a living guard. */
    hand[0] = 5; hand[1] = 5;
    assert(gevrLootDistanceSquared(hand,front,back,0) < 400);
    /* Degenerate or missing torso depth falls back to the actual magazine. */
    assert(fabsf(gevrLootDistanceSquared(hand,front,front,1) - 50) < .001f);
    puts("corpse loot: front, face-down back, bounds and degenerate torso passed");
    return 0;
}
