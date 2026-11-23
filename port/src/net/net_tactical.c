#include "net_core.h"
#include "net_coop.h"
#include "net/netbuf.h"
#include "game/player.h"
#include "gevr_tactical_mesh.h"
#include "gevr_tactical_net.h"
#include "system.h"

enum {
    LAYOUT_MAX = 16 + GEVR_VIS_GUNS * (3 + 2 * (GEVR_GEAR_MOUNTS - 1)),
    STATE_MAX = 256,
    STATE_INTERVAL_US = 33333
};

typedef struct NetTacticalVisual {
    GevrTacticalVisual view;
    uint32_t epoch;
    uint16_t revision;
    uint8_t stateReady;
} NetTacticalVisual;

static NetTacticalVisual s_visuals[GEVR_MAX_PLAYERS];
static uint64_t s_lastSend;
static uint32_t s_epoch;
static int s_active;

static int enabled(void)
{
    return netCoopSession() && netCoopActive() && gevrTacticalGearEnabled();
}

void gevrTacticalVisualReset(void)
{
    memset(s_visuals, 0, sizeof(s_visuals));
    s_lastSend = 0;
    s_epoch = netGetWorldEpoch();
    s_active = enabled();
}

void gevrTacticalVisualForgetSlot(int slot)
{
    if (slot >= 0 && slot < GEVR_MAX_PLAYERS)
        s_visuals[slot] = (NetTacticalVisual){0};
}

static void writeHeader(struct netbuf *packet, int msg, int slot, const NetTacticalVisual *visual)
{
    netbufStartWrite(packet);
    netbufWriteU32(packet, GEVR_NET_MAGIC);
    netbufWriteU16(packet, GEVR_NET_VERSION);
    netbufWriteU8(packet, msg);
    netbufWriteU8(packet, slot);
    netbufWriteU32(packet, visual->epoch);
    netbufWriteU16(packet, visual->revision);
}

void gevrTacticalWriteVisualBuild(struct netbuf *packet, int item, int build, int stock)
{
    GevrGearSelection selection;
    gevrTacticalGearPackSelection(build, stock, &selection);
    netbufWriteU8(packet, item);
    netbufWriteU8(packet, selection.key);
    netbufWriteU8(packet, selection.count);
    for (int i = 0; i < selection.count; ++i) netbufWriteU16(packet, selection.parts[i]);
}

static void writeLayout(struct netbuf *packet, int slot, const NetTacticalVisual *visual)
{
    writeHeader(packet, NET_MSG_TACTICAL_LAYOUT, slot, visual);
    for (int i = 0; i < GEVR_VIS_GUNS; ++i) {
        const GevrVisualWeapon *gun = &visual->view.weapons[i];
        gevrTacticalWriteVisualBuild(packet, gun->item, gun->build, gun->stock);
    }
}

void gevrTacticalNetWriteMagazine(struct netbuf *packet, const TacticalMagazine *mag)
{
    netbufWriteU8(packet, mag->id ? mag->type : 0);
    if (!mag->id || !mag->type) return;
    netbufWriteU16(packet, mag->capacity);
    netbufWriteU16(packet, mag->rounds);
    netbufWriteU16(packet, mag->gear_part);
}

static void writeState(struct netbuf *packet, int slot, const NetTacticalVisual *visual)
{
    writeHeader(packet, NET_MSG_TACTICAL_VISUAL, slot, visual);
    netbufWriteU8(packet, visual->view.body);
    for (int i = 0; i < GEVR_VIS_GUNS; ++i) {
        const GevrVisualWeapon *gun = &visual->view.weapons[i];
        if (!gun->item) continue;
        netbufWriteU8(packet, gun->flags);
        gevrTacticalNetWriteMagazine(packet, &gun->magazine);
        if (i > GEVR_VIS_LEFT) continue;
        netbufWriteU8(packet, gun->action);
        netbufWriteU8(packet, gun->manual);
        netbufWriteU16(packet, gun->shots);
        if (gun->build >= 0) netbufWriteData(packet, gun->mounts, sizeof(gun->mounts));
    }
    for (int i = 0; i < GEVR_VIS_MAGS; ++i)
        gevrTacticalNetWriteMagazine(packet, &visual->view.magazines[i]);
}

int gevrTacticalNetReadMagazine(struct netbuf *packet, TacticalMagazine *out)
{
    TacticalMagazine mag = {0};
    mag.type = netbufReadU8(packet);
    if (mag.type) {
        mag.id = 1;
        mag.capacity = netbufReadU16(packet);
        mag.rounds = netbufReadU16(packet);
        mag.gear_part = netbufReadU16(packet);
        if (mag.rounds > mag.capacity || mag.capacity > 300
            || tacticalMagazineGun(&mag) < 0
            || (mag.gear_part && !gevrTacticalGearPart(mag.gear_part - 1))) return 0;
    }
    if (mag.id)
        mag.mag_class = tacticalGunIsPistol(tacticalMagazineGun(&mag)) ? TACTICAL_MAG_PISTOL : TACTICAL_MAG_PRIMARY;
    *out = mag;
    return !packet->error;
}

int gevrTacticalReadVisualBuild(struct netbuf *packet, GevrVisualWeapon *gun)
{
    GevrGearSelection selection = {0};
    gun->item = netbufReadU8(packet);
    gun->build = -1;
    selection.key = netbufReadU8(packet);
    selection.count = netbufReadU8(packet);
    if (packet->error || gun->item >= ITEM_IDS_MAX || selection.count >= GEVR_GEAR_MOUNTS) return 0;
    if (!selection.count) return 1;
    const int kind = gevrTacticalItem(gun->item);
    if (kind < 0 || (selection.key != gevrTacticalGearKey(kind)
        && !(kind == TACTICAL_GUN_USP && selection.key == GEVR_GEAR_PPK_SUPPRESSED))) return 0;
    for (int i = 0; i < selection.count; ++i) selection.parts[i] = netbufReadU16(packet);
    if (packet->error) return 0;
    gun->build = gevrTacticalGearReceiveSelection(&selection);
    return gun->build >= 0;
}

static int readLayout(struct netbuf *packet, GevrTacticalVisual *view)
{
    for (int i = 0; i < GEVR_VIS_GUNS; ++i)
        if (!gevrTacticalReadVisualBuild(packet, &view->weapons[i])) return 0;
    return !netbufReadLeft(packet);
}

static int readState(struct netbuf *packet, GevrTacticalVisual *view)
{
    view->body = netbufReadU8(packet);
    if (view->body & ~(GEVR_VIS_KNIFE | GEVR_VIS_KATANA | GEVR_VIS_LEFT_HANDED)) return 0;
    for (int i = 0; i < GEVR_VIS_GUNS; ++i) {
        GevrVisualWeapon *gun = &view->weapons[i];
        if (!gun->item) continue;
        gun->flags = netbufReadU8(packet);
        if (gun->flags & ~(GEVR_VIS_CHAMBER | GEVR_VIS_SUPPRESSED) || !gevrTacticalNetReadMagazine(packet, &gun->magazine)) return 0;
        if (i > GEVR_VIS_LEFT) continue;
        gun->action = netbufReadU8(packet);
        gun->manual = netbufReadU8(packet);
        gun->shots = netbufReadU16(packet);
        if (gun->action > 100 || gun->manual > 50) return 0;
        if (gun->build >= 0) netbufReadData(packet, gun->mounts, sizeof(gun->mounts));
    }
    for (int i = 0; i < GEVR_VIS_MAGS; ++i)
        if (!gevrTacticalNetReadMagazine(packet, &view->magazines[i])) return 0;
    return !packet->error && !netbufReadLeft(packet);
}

#include "game/gevr_tactical_guard_ai.h"
#include "game/gevr_tactical_loadouts.h"

int gevrTacticalVisualReceive(int slot, int msg, struct netbuf *packet)
{
    if (!enabled() || slot < 0 || slot >= GEVR_MAX_PLAYERS || slot == netGetLocalSlot()) return 0;
    if (!s_active || s_epoch != netGetWorldEpoch()) gevrTacticalVisualReset();
    const uint32_t epoch = netbufReadU32(packet);
    const uint16_t revision = netbufReadU16(packet);
    if (packet->error || epoch != netGetWorldEpoch() || !revision) return 0;
    NetTacticalVisual *remote = &s_visuals[slot];
    if (msg == NET_MSG_TACTICAL_LAYOUT) {
        GevrTacticalVisual view = {0};
        if (!readLayout(packet, &view)) return 0;
        *remote = (NetTacticalVisual){.view = view, .epoch = epoch, .revision = revision};
        if (sysArgCheck("--tactical-coop-test")) {
            for (int i = 0; i < GEVR_VIS_GUNS; ++i) {
                const GevrGearAssembly *assembly = gevrTacticalGearDeployedAssembly(view.weapons[i].build);
                if (assembly) sysLogPrintf(LOG_NOTE, "tactical co-op: received slot %d mount %d item %d build %d (%d fittings)",
                    slot, i, view.weapons[i].item, view.weapons[i].build, assembly->count - 1);
            }
        }
    } else {
        if (remote->epoch != epoch || remote->revision != revision) return 0;
        GevrTacticalVisual view = remote->view;
        if (!readState(packet, &view)) return 0;
        if (sysArgCheck("--tactical-coop-test")) {
            const GevrVisualWeapon *fresh = &view.weapons[GEVR_VIS_RIGHT];
            const GevrVisualWeapon *old = &remote->view.weapons[GEVR_VIS_RIGHT];
            if (fresh->magazine.id != old->magazine.id)
                sysLogPrintf(LOG_NOTE, "tactical co-op: received slot %d right magazine %u rounds %u",
                    slot, fresh->magazine.id, fresh->magazine.rounds);
            if (fresh->manual != old->manual)
                sysLogPrintf(LOG_NOTE, "tactical co-op: received slot %d right manual travel %u",
                    slot, fresh->manual);
        }
        if (netIsHost() && remote->stateReady) {
            for (int i = GEVR_VIS_RIGHT; i <= GEVR_VIS_LEFT; i++) {
                const GevrVisualWeapon *gun = &view.weapons[i];
                const uint16_t newShots = gun->shots - remote->view.weapons[i].shots;
                if (newShots && newShots < 64 && gevrCampaignGunIsFirearm(gun->item))
                    gevrTacticalGuardNoise(slot, gun->flags & GEVR_VIS_SUPPRESSED ? 1200.0f : 3200.0f);
            }
        }
        remote->view = view;
        remote->stateReady = 1;
    }
    return 1;
}

static void sendLayout(int who, int to, const NetTacticalVisual *look)
{
    uint8_t raw[LAYOUT_MAX];
    struct netbuf packet = {.data = raw, .size = sizeof(raw)};
    writeLayout(&packet, who, look);
    if (packet.error) return;
    if (to < 0) netCoopBroadcast(raw, packet.wp, 1);
    else netCoopSendTo(to, raw, packet.wp);
}

static void sendState(int who, int to, const NetTacticalVisual *look)
{
    uint8_t raw[STATE_MAX];
    struct netbuf packet = {.data = raw, .size = sizeof(raw)};
    writeState(&packet, who, look);
    if (packet.error) return;
    if (to < 0) netCoopBroadcast(raw, packet.wp, 0);
    else netCoopSendTo(to, raw, packet.wp);
}

void gevrTacticalVisualSendTo(int slot)
{
    if (!enabled()) return;
    for (int who = 0; who < GEVR_MAX_PLAYERS; ++who) {
        const NetTacticalVisual *look = &s_visuals[who];
        if (who == slot || !netLobbySlotConnected(who) || !look->revision || look->epoch != netGetWorldEpoch()) continue;
        sendLayout(who, slot, look);
        sendState(who, slot, look);
    }
}

void gevrTacticalVisualTick(void)
{
    const int active = enabled();
    if (active != s_active || s_epoch != netGetWorldEpoch()) gevrTacticalVisualReset();
    if (!active) return;
    const int slot = netGetLocalSlot();
    if (slot < 0 || slot >= GEVR_MAX_PLAYERS || !g_playerPointers[slot] || netSlotIsSpectator(slot)) return;
    const uint64_t now = sysGetMicroseconds();
    if (now - s_lastSend < STATE_INTERVAL_US) return;
    s_lastSend = now;

    const int previous = get_cur_playernum();
    if (previous != slot) set_cur_player(slot);
    GevrTacticalVisual view = {0};
    gevrTacticalCollectVisual(&view);
    if (previous != slot) set_cur_player(previous);

    NetTacticalVisual *local = &s_visuals[slot];
    int changed = !local->revision;
    for (int i = 0; i < GEVR_VIS_GUNS && !changed; ++i) {
        const GevrVisualWeapon *a = &view.weapons[i], *b = &local->view.weapons[i];
        changed = a->item != b->item || a->build != b->build || a->stock != b->stock;
    }
    local->view = view;
    local->stateReady = 1;
    local->epoch = s_epoch;
    if (changed) {
        if (!++local->revision) ++local->revision;
        sendLayout(slot, -1, local);
    }
    sendState(slot, -1, local);
}

static const GevrTacticalVisual *playerVisual(int slot)
{
    if (!netIsRemotePlayerActive(slot) || netSlotIsSpectator(slot)) return NULL;
    const NetTacticalVisual *remote = &s_visuals[slot];
    return remote->stateReady && remote->epoch == netGetWorldEpoch() ? &remote->view : NULL;
}

const GevrTacticalVisual *gevrTacticalRemoteBody(const ChrRecord *chr)
{
    if (!enabled() || !chr || !chr->prop || chr->prop->type != PROP_TYPE_VIEWER) return NULL;
    return playerVisual(getPlayerPointerIndex(chr->prop));
}

const GevrVisualWeapon *gevrTacticalRemoteWeapon(const Model *model)
{
    if (!model || !enabled() || !model->attachedto_objinst) return NULL;
    for (int slot = 0; slot < getPlayerCount(); ++slot) {
        const GevrTacticalVisual *look = playerVisual(slot);
        if (!look) continue;
        const struct player *pl = g_playerPointers[slot];
        if (!pl || !pl->prop || !pl->prop->chr) continue;
        const ChrRecord *chr = pl->prop->chr;
        for (int hand = GUNRIGHT; hand <= GUNLEFT; ++hand) {
            const PropRecord *prop = chr->weapons_held[hand];
            const GevrVisualWeapon *gun = &look->weapons[hand];
            if (prop && prop->weapon && prop->obj->model == model && prop->weapon->weaponnum == gun->item)
                return gun;
        }
    }
    return NULL;
}
