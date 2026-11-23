#include "gevr_tactical_inventory.h"
#include "gevr_tactical_rig_bounds.h"
#include <assert.h>
#include <stdio.h>

static void far(TacticalInteractionInput *in)
{
    *in = (TacticalInteractionInput){0};
    in->tracked = 1; in->grab_radius_cm = 20; in->seat_radius_cm = 15;
    in->rig_pickup_bounds_valid = 1; in->rig_pickup_radius_cm = 1.5f;
    in->magazine_distance_cm = in->action_distance_cm = 100;
    for (int k = 0; k < 5; k++) in->rig_distance_cm[k] = in->rig_pickup_distance_cm[k] = 100;
}
int main(void)
{
    const float wrist[3] = {0, 0, 0}, right[3] = {1, 0, 0};
    const float up[3] = {0, 1, 0}, back[3] = {0, 0, 1};
    float tip[3];
    gevrTacticalFingertip(wrist, right, up, back, 1, 1, 0, tip);
    assert(fabsf(tip[0] + 2.33f) < .001f && fabsf(tip[1] + 3.5f) < .001f
        && fabsf(tip[2] + 7.88f) < .001f);
    gevrTacticalFingertip(wrist, right, up, back, 1, 1, 1, tip);
    assert(fabsf(tip[0] - 2.33f) < .001f);
    float low[3] = {-20, -80, -30}, high[3] = {20, 80, 30};
    const float scale = .085f;
    TacticalInventory inv; TacticalInteraction interaction; TacticalInteractionInput in;
    TacticalMagazine dropped = {0}, a = {1,1,30,21,TACTICAL_MAG_PRIMARY}, b = {2,1,30,14,TACTICAL_MAG_PRIMARY};
    unsigned ejected = 0;
    /* Reproduce the distances seen in the user's unsuccessful G2 session. */
    const float observed[5] = {11.3f, 11.4f, 18.3f, 21.4f, 30.9f};
    const float emptySlot[5] = {2.4f, 10.8f, 20.7f, 13.7f, 34.6f};
    const float away[5] = {12.1f, 20, 25, 30, 40};
    assert(gevrTacticalRigNearestSlot(observed, 12) == 0);
    assert(gevrTacticalRigNearestSlot(emptySlot, 12) == 0);
    assert(gevrTacticalRigNearestSlot(away, 12) == -1);
    assert(gevrTacticalRigSurfaceDistance(5, 0, 0, low, high, scale) == 0);
    assert(gevrTacticalRigSurfaceDistance(0, 4, 0, low, high, scale) > 2.2f);
    assert(gevrTacticalRigSurfaceDistance(0, 0, -4, low, high, scale) > 1.4f);
    /* Use the same axes as drawing, with the torso already turned in view. */
    float pose[4][3] = {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}, {2, -8, -3}};
    gevrTacticalFlatRigAxes(pose);
    assert(pose[0][0] == 1 && pose[1][1] == 1 && pose[2][2] == 1);
    const float halfCm[3] = {1.5f, 9.f, 3.5f};
    const float cm = .2f;
    const float onEnd[3] = {2.f, -8.f + 8.f * cm, -3.f - 2.f * cm};
    const float beside[3] = {2.f, -8.f, -3.f - 7.f * cm};
    const float inFront[3] = {2.f + 6.f * cm, -8.f, -3.f};
    assert(gevrTacticalFlatRigDistance(onEnd, pose, cm, halfCm) < .001f);
    assert(gevrTacticalFlatRigDistance(beside, pose, cm, halfCm) > 3.f);
    assert(gevrTacticalFlatRigDistance(inFront, pose, cm, halfCm) > 4.f);
    tacticalInventoryInit(&inv);
    assert(tacticalSetWell(&inv, TACTICAL_PRIMARY_WELL, 1) == TACTICAL_OK);
    assert(tacticalAddMagazine(&inv, TACTICAL_RIG_PRIMARY_1, a) == TACTICAL_OK);
    assert(tacticalAddMagazine(&inv, TACTICAL_RIG_PRIMARY_2, b) == TACTICAL_OK);
    tacticalInteractionInit(&interaction, TACTICAL_PRIMARY_WELL, TACTICAL_LEFT_HAND);
    far(&in); in.grip = 1; in.rig_distance_cm[0] = 6; in.rig_pickup_distance_cm[0] = 4;
    assert(tacticalInteractionTick(&interaction, &inv, &in, &dropped, &ejected) == TACTICAL_EVENT_NONE);
    assert(inv.slots[TACTICAL_RIG_PRIMARY_1].id == a.id);
    in.grip = 0; tacticalInteractionTick(&interaction, &inv, &in, &dropped, &ejected);
    far(&in); in.grip = 1;
    /* With the wrist beyond the generic grab radius, the fingertip on
     * mag 2 still wins over a nearer centre and a nearby gun contact. */
    in.rig_distance_cm[0] = 4; in.rig_pickup_distance_cm[0] = 1;
    in.action_distance_cm = 3;
    in.rig_distance_cm[1] = 25; in.rig_pickup_distance_cm[1] = 0;
    assert(tacticalInteractionTick(&interaction, &inv, &in, &dropped, &ejected) == TACTICAL_EVENT_MAG_TAKEN);
    assert(inv.slots[TACTICAL_LEFT_HAND].id == b.id && inv.slots[TACTICAL_LEFT_HAND].rounds == 14);
    /* Returning a magazine still has the original forgiving 20 cm range. */
    far(&in); in.rig_distance_cm[1] = 12; in.rig_pickup_distance_cm[1] = 8;
    assert(tacticalInteractionTick(&interaction, &inv, &in, &dropped, &ejected) == TACTICAL_EVENT_MAG_STORED);
    assert(inv.slots[TACTICAL_RIG_PRIMARY_2].id == b.id);
    puts("rig grab: flat pose, visible contact, fingertip at magazine end, and forgiving storage");
    return 0;
}
