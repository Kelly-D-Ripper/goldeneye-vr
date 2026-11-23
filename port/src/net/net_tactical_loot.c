#include "gevr_tactical_loot_net.h"
#include "gevr_tactical_net.h"
#include "gevr_tactical_mesh.h"
#include "net_core.h"
#include "net_coop.h"
#include "net/netbuf.h"
#include "game/chrai.h"
#include "game/player.h"
#include "system.h"
#include <math.h>

enum { PUBLISH = 1, SPAWN, MOVE, CLAIM, GRANT, FINISH, REMOVE, DECLINE, SNAPSHOT_BEGIN, SNAPSHOT_END };
enum {
    MAX_WORLD_LOOT = MAX_WEAPON_SLOTS + GEVR_WORLD_MAGAZINE_CAPACITY,
    LOOT_INTERVAL_US = 83333,
    LOOT_PACKET_MAX = 96 + 2 * GEVR_GEAR_MOUNTS
};

typedef struct WorldLoot {
    GevrSharedLoot loot;
    uint32_t motion;
    int moving, presented, seen, simulatedHere;
} WorldLoot;

typedef struct LootClaim {
    uint32_t id;
    uint8_t ctrl;
} LootClaim;

static WorldLoot s_world[MAX_WORLD_LOOT];
static LootClaim s_claims[GEVR_MAX_PLAYERS];
static uint32_t s_dropCounter;
static uint32_t s_lastDrop[GEVR_MAX_PLAYERS];
static uint32_t s_pending[2];
static uint64_t s_lastTick;
static int s_receivingSnapshot;

int gevrTacticalLootEnabled(void)
{
    return netCoopSession() && netCoopActive() && gevrTacticalGearEnabled();
}

void gevrTacticalLootStageReset(void)
{
    memset(s_world, 0, sizeof(s_world));
    memset(s_claims, 0, sizeof(s_claims));
    memset(s_lastDrop, 0, sizeof(s_lastDrop));
    memset(s_pending, 0, sizeof(s_pending));
    s_dropCounter = 0;
    s_lastTick = 0;
    s_receivingSnapshot = 0;
}

static int ownerOf(uint32_t id)
{
    return (id >> 27) & 7;
}

static WorldLoot *findWorld(uint32_t id)
{
    if (!id) return NULL;
    for (int i = 0; i < MAX_WORLD_LOOT; ++i)
        if (s_world[i].loot.id == id) return &s_world[i];
    return NULL;
}

static WorldLoot *freeWorld(void)
{
    for (int i = 0; i < MAX_WORLD_LOOT; ++i)
        if (!s_world[i].loot.id) return &s_world[i];
    return NULL;
}

int gevrTacticalLootCanPublish(void)
{
    return !gevrTacticalLootEnabled() || freeWorld() != NULL;
}

static void startPacket(struct netbuf *packet, int op, uint32_t id)
{
    netbufStartWrite(packet);
    netbufWriteU32(packet, GEVR_NET_MAGIC);
    netbufWriteU16(packet, GEVR_NET_VERSION);
    netbufWriteU8(packet, NET_MSG_TACTICAL_LOOT);
    netbufWriteU8(packet, netGetLocalSlot());
    netbufWriteU32(packet, netGetWorldEpoch());
    netbufWriteU8(packet, op);
    netbufWriteU32(packet, id);
}

static void writePoint(struct netbuf *packet, const float point[3])
{
    for (int i = 0; i < 3; ++i) netbufWriteF32(packet, point[i]);
}

static int readPoint(struct netbuf *packet, float point[3])
{
    for (int i = 0; i < 3; ++i) {
        point[i] = netbufReadF32(packet);
        if (!isfinite(point[i]) || fabsf(point[i]) > 1000000.f) return 0;
    }
    return !packet->error;
}

static void writeOrientation(struct netbuf *packet, const GevrSharedLoot *loot)
{
    for (int row = 0; row < 3; ++row)
        writePoint(packet, loot->orientation[row]);
}

static int readOrientation(struct netbuf *packet, GevrSharedLoot *loot)
{
    for (int row = 0; row < 3; ++row) {
        if (!readPoint(packet, loot->orientation[row])) return 0;
        float squared = 0;
        for (int col = 0; col < 3; ++col)
            squared += loot->orientation[row][col] * loot->orientation[row][col];
        if (squared < 1e-10f || squared > 1024.f) return 0;
    }
    return 1;
}

static void writeLoot(struct netbuf *packet, const GevrSharedLoot *loot)
{
    netbufWriteU8(packet, loot->kind);
    if (loot->kind == GEVR_LOOT_GUN) {
        gevrTacticalWriteVisualBuild(packet, loot->item, loot->build, 0);
        netbufWriteU8(packet, loot->fireMode);
        netbufWriteU8(packet, loot->chamber);
        netbufWriteU16(packet, loot->nativeRounds);
        netbufWriteU8(packet, loot->nativeMissing);
        writeOrientation(packet, loot);
    }
    gevrTacticalNetWriteMagazine(packet, &loot->magazine);
    writePoint(packet, loot->position);
    netbufWriteU8(packet, loot->flat);
    netbufWriteU8(packet, loot->magnet);
}

static int readLoot(struct netbuf *packet, GevrSharedLoot *loot)
{
    loot->kind = netbufReadU8(packet);
    loot->build = -1;
    if (loot->kind == GEVR_LOOT_GUN) {
        GevrVisualWeapon gun = {0};
        if (!gevrTacticalReadVisualBuild(packet, &gun)) return 0;
        loot->item = gun.item;
        loot->build = gun.build;
        if (gevrTacticalItem(gun.item) < 0 && !gevrTacticalNativeRifle(gun.item)) return 0;
        loot->fireMode = netbufReadU8(packet);
        loot->chamber = netbufReadU8(packet);
        loot->nativeRounds = netbufReadU16(packet);
        loot->nativeMissing = netbufReadU8(packet);
        if (loot->fireMode > 7 || loot->chamber > 1 || loot->nativeRounds > 300 || loot->nativeMissing > 1
            || !readOrientation(packet, loot)) return 0;
    } else if (loot->kind != GEVR_LOOT_MAG && loot->kind != GEVR_LOOT_RIG) return 0;
    if (!gevrTacticalNetReadMagazine(packet, &loot->magazine)) return 0;
    if (loot->kind != GEVR_LOOT_GUN && !loot->magazine.id) return 0;
    if (!readPoint(packet, loot->position)) return 0;
    loot->flat = netbufReadU8(packet);
    loot->magnet = netbufReadU8(packet);
    return !packet->error && loot->flat <= 2 && loot->magnet <= 1;
}

static void sendLoot(int op, const GevrSharedLoot *loot, int to, int value)
{
    uint8_t raw[LOOT_PACKET_MAX];
    struct netbuf packet = {.data = raw, .size = sizeof(raw)};
    startPacket(&packet, op, loot->id);
    if (op == GRANT) netbufWriteU8(&packet, value);
    writeLoot(&packet, loot);
    if (packet.error) return;
    if (to >= 0) netCoopSendTo(to, raw, packet.wp);
    else netCoopBroadcast(raw, packet.wp, 1);
}

static void sendControl(int op, uint32_t id, int to, int value)
{
    uint8_t raw[24];
    struct netbuf packet = {.data = raw, .size = sizeof(raw)};
    startPacket(&packet, op, id);
    if (op != REMOVE) netbufWriteU8(&packet, value);
    if (to >= 0) netCoopSendTo(to, raw, packet.wp);
    else netCoopBroadcast(raw, packet.wp, 1);
}

static void sendMotion(const WorldLoot *entry, int moving)
{
    uint8_t raw[72];
    struct netbuf packet = {.data = raw, .size = sizeof(raw)};
    startPacket(&packet, MOVE, entry->loot.id);
    writePoint(&packet, entry->loot.position);
    if (entry->loot.kind == GEVR_LOOT_GUN) writeOrientation(&packet, &entry->loot);
    netbufWriteU32(&packet, entry->motion);
    netbufWriteU8(&packet, moving);
    netCoopBroadcast(raw, packet.wp, !moving);
}

static int rememberWorld(const GevrSharedLoot *loot, int local)
{
    WorldLoot *entry = findWorld(loot->id);
    if (entry) {
        entry->seen = 1;
        if (!entry->simulatedHere) {
            entry->loot = *loot;
            if (entry->presented) gevrTacticalLootMoveWorld(loot);
        }
        return 1;
    }
    entry = freeWorld();
    if (!entry) return 0;
    *entry = (WorldLoot){.loot = *loot, .presented = local, .seen = 1, .simulatedHere = local};
    const int owner = ownerOf(loot->id);
    if (loot->id > s_lastDrop[owner]) s_lastDrop[owner] = loot->id;
    return 1;
}

uint32_t gevrTacticalLootPublish(const GevrSharedLoot *source)
{
    if (!gevrTacticalLootEnabled()) return 0;
    const int owner = netGetLocalSlot();
    if (owner < 0 || owner >= GEVR_MAX_PLAYERS || s_dropCounter >= 0x07ffffffu) return 0;
    GevrSharedLoot loot = *source;
    loot.id = ((uint32_t)owner << 27) | ++s_dropCounter;
    if (!rememberWorld(&loot, 1)) return 0;
    sendLoot(netIsHost() ? SPAWN : PUBLISH, &loot, -1, 0);
    if (sysArgCheck("--tactical-coop-test"))
        sysLogPrintf(LOG_NOTE, "tactical co-op: published loot %u kind %d item %d build %d rounds %u chamber %d at %.2f %.2f %.2f",
            loot.id, loot.kind, loot.item, loot.build, loot.magazine.rounds, loot.chamber,
            (double)loot.position[0], (double)loot.position[1], (double)loot.position[2]);
    return loot.id;
}

void gevrTacticalLootSendTo(int slot)
{
    if (!gevrTacticalLootEnabled()) return;
    const uint32_t frontier = s_lastDrop[slot] ? s_lastDrop[slot] : 1;
    sendControl(SNAPSHOT_BEGIN, frontier, slot, 0);
    for (int i = 0; i < MAX_WORLD_LOOT; ++i)
        if (s_world[i].loot.id) sendLoot(SPAWN, &s_world[i].loot, slot, 0);
    sendControl(SNAPSHOT_END, 1, slot, 0);
}

static int playerNear(int slot, const float point[3])
{
    if (slot < 0 || slot >= GEVR_MAX_PLAYERS || netSlotIsSpectator(slot)) return 0;
    const struct player *player = g_playerPointers[slot];
    if (!player || !player->prop || player->bonddead) return 0;
    const float reach = 180.f; /* room for tracked reach and walking across a pose update */
    float distance = 0;
    for (int axis = 0; axis < 3; ++axis) {
        const float d = player->prop->pos.f[axis] - point[axis];
        distance += d * d;
    }
    return distance <= reach * reach;
}

int gevrTacticalLootReserved(uint32_t id)
{
    if (id)
        for (int i = 0; i < GEVR_MAX_PLAYERS; ++i)
            if (s_claims[i].id == id) return 1;
    return 0;
}

void gevrTacticalLootRetireRig(uint32_t id)
{
    if ((id & GEVR_RIG_LOOT_ID) && netIsHost() && gevrTacticalLootEnabled())
        sendControl(REMOVE, id, -1, 0);
}

static int readClaim(uint32_t id, GevrSharedLoot *loot)
{
    if (gevrTacticalLootReserved(id)) return 0;
    if (id & GEVR_RIG_LOOT_ID) return gevrTacticalLootReadRig(id, loot);
    const WorldLoot *entry = findWorld(id);
    if (!entry) return 0;
    *loot = entry->loot;
    return 1;
}

static void erase(uint32_t id)
{
    if (id & GEVR_RIG_LOOT_ID) {
        gevrTacticalLootEraseRig(id);
    } else {
        WorldLoot *entry = findWorld(id);
        if (!entry) return;
        gevrTacticalLootEraseWorld(id);
        *entry = (WorldLoot){0};
    }
    for (int hand = 0; hand < 2; ++hand)
        if (s_pending[hand] == id) s_pending[hand] = 0;
}

static int takeLocal(const GevrSharedLoot *loot, int ctrl)
{
    const int own = netGetLocalSlot();
    if (own < 0 || own >= GEVR_MAX_PLAYERS || netSlotIsSpectator(own) || !g_playerPointers[own]) return 0;
    const int previous = get_cur_playernum();
    if (own != previous) set_cur_player(own);
    int accepted = gevrTacticalLootTakeLocal(loot, ctrl);
    if (own != previous) set_cur_player(previous);
    return accepted;
}

static int hostClaim(uint32_t id, int slot, int ctrl)
{
    GevrSharedLoot loot = {0};
    if (slot < 0 || slot >= GEVR_MAX_PLAYERS || s_claims[slot].id
        || !readClaim(id, &loot) || !playerNear(slot, loot.position)) return 0;
    if (slot == netGetLocalSlot()) {
        if (!takeLocal(&loot, ctrl)) return 0;
        erase(id);
        sendControl(REMOVE, id, -1, 0);
        return 1;
    }
    s_claims[slot] = (LootClaim){.id = id, .ctrl = ctrl};
    sendLoot(GRANT, &loot, slot, ctrl);
    return 1;
}

int gevrTacticalLootClaim(uint32_t id, int ctrl)
{
    if (!gevrTacticalLootEnabled() || !id || ctrl < 0 || ctrl > 1) return 0;
    if (netIsHost()) return hostClaim(id, netGetLocalSlot(), ctrl);
    if (s_pending[0] || s_pending[1]) return 1;
    s_pending[ctrl] = id;
    sendControl(CLAIM, id, -1, ctrl);
    return 1;
}

void gevrTacticalLootForgetSlot(int slot)
{
    if (slot < 0 || slot >= GEVR_MAX_PLAYERS) return;
    if (slot == netGetLocalSlot()) memset(s_pending, 0, sizeof(s_pending));
    if (netIsHost() && s_claims[slot].id) {
        /* A departing claimant may have accepted the grant before losing its
         * connection. Do not mint another copy of the item. */
        const uint32_t id = s_claims[slot].id;
        erase(id);
        sendControl(REMOVE, id, -1, 0);
    }
    s_claims[slot] = (LootClaim){0};
}

void gevrTacticalLootReceive(struct netbuf *packet, int sender)
{
    if (!gevrTacticalLootEnabled() || sender < 0 || sender >= GEVR_MAX_PLAYERS) return;
    const uint32_t epoch = netbufReadU32(packet);
    const int op = netbufReadU8(packet);
    const uint32_t id = netbufReadU32(packet);
    if (packet->error || epoch != netGetWorldEpoch() || !id) return;
    const int host = netIsHost();
    const int own = netGetLocalSlot();
    if ((host && (op == PUBLISH || op == MOVE) && (id & GEVR_RIG_LOOT_ID || ownerOf(id) != sender))
        || (!host && sender != netGetHostSlot())) return;

    switch (op) {
    case SNAPSHOT_BEGIN:
    case SNAPSHOT_END: {
        const unsigned reserved = netbufReadU8(packet);
        if (host || reserved || packet->error || netbufReadLeft(packet)) break;
        if (op == SNAPSHOT_BEGIN) {
            s_receivingSnapshot = 1;
            const uint32_t seen = id & UINT32_C(0x07ffffff);
            if (seen > s_dropCounter) s_dropCounter = seen;
            for (int i = 0; i < MAX_WORLD_LOOT; ++i) s_world[i].seen = 0;
        } else if (s_receivingSnapshot) {
            for (int i = 0; i < MAX_WORLD_LOOT; ++i)
                if (s_world[i].loot.id && !s_world[i].seen) erase(s_world[i].loot.id);
            s_receivingSnapshot = 0;
            memset(s_pending, 0, sizeof(s_pending));
            if (sysArgCheck("--tactical-coop-test")) sysLogPrintf(LOG_NOTE, "tactical co-op: loot snapshot applied");
        }
        break;
    }
    case PUBLISH:
    case SPAWN: {
        if (host != (op == PUBLISH) || (id & GEVR_RIG_LOOT_ID)) break;
        if (host && id <= s_lastDrop[sender]) break;
        GevrSharedLoot loot = {.id = id};
        if (!readLoot(packet, &loot) || netbufReadLeft(packet) || loot.kind == GEVR_LOOT_RIG) break;
        if (host && (!playerNear(sender, loot.position) || !freeWorld())) {
            sendControl(DECLINE, id, sender, 2);  /* The owner still has its local object. */
            break;
        }
        if (rememberWorld(&loot, 0)) {
            if (host) sendLoot(SPAWN, &loot, -1, 0);
            if (sysArgCheck("--tactical-coop-test"))
                sysLogPrintf(LOG_NOTE, "tactical co-op: received loot %u kind %d item %d build %d rounds %u chamber %d",
                    id, loot.kind, loot.item, loot.build, loot.magazine.rounds, loot.chamber);
        }
        break;
    }
    case MOVE: {
        WorldLoot *entry = findWorld(id);
        if (!entry || entry->simulatedHere) break;
        GevrSharedLoot updated = entry->loot;
        if (!readPoint(packet, updated.position)
            || (updated.kind == GEVR_LOOT_GUN && !readOrientation(packet, &updated))) break;
        const uint32_t sequence = netbufReadU32(packet);
        const int moving = netbufReadU8(packet);
        if (packet->error || netbufReadLeft(packet) || moving > 1 || (int32_t)(sequence - entry->motion) <= 0) break;
        entry->motion = sequence;
        entry->loot = updated;
        if (entry->presented) gevrTacticalLootMoveWorld(&entry->loot);
        if (host) sendMotion(entry, moving);
        break;
    }
    case CLAIM: {
        if (!host) break;
        const int ctrl = netbufReadU8(packet);
        if (packet->error || netbufReadLeft(packet) || ctrl > 1) break;
        if (!hostClaim(id, sender, ctrl)) {
            sendControl(DECLINE, id, sender, ctrl);
            if (sysArgCheck("--tactical-coop-test"))
                sysLogPrintf(LOG_NOTE, "tactical co-op: refused claim on loot %u from slot %d", id, sender);
        }
        break;
    }
    case GRANT: {
        if (host) break;
        const int ctrl = netbufReadU8(packet);
        GevrSharedLoot loot = {.id = id};
        if (ctrl > 1 || !readLoot(packet, &loot) || netbufReadLeft(packet)) break;
        const int pending = s_pending[ctrl] == id;
        const int taken = pending && takeLocal(&loot, ctrl);
        if (pending) s_pending[ctrl] = 0;
        sendControl(FINISH, id, -1, taken);
        if (taken) erase(id);
        break;
    }
    case FINISH: {
        if (!host || s_claims[sender].id != id) break;
        const int taken = netbufReadU8(packet);
        if (packet->error || netbufReadLeft(packet) || taken > 1) break;
        s_claims[sender] = (LootClaim){0};
        if (taken) {
            erase(id);
            sendControl(REMOVE, id, -1, 0);
        }
        break;
    }
    case REMOVE:
        if (!host && !netbufReadLeft(packet)) erase(id);
        break;
    case DECLINE: {
        const int ctrl = netbufReadU8(packet);
        if (host || packet->error || netbufReadLeft(packet)) break;
        if (ctrl <= 1 && s_pending[ctrl] == id) {
            s_pending[ctrl] = 0;
        } else if (ctrl == 2 && ownerOf(id) == own) {
            WorldLoot *entry = findWorld(id);
            if (!entry || !entry->simulatedHere) break;
            gevrTacticalLootUnshareWorld(id);
            *entry = (WorldLoot){0};
            for (int hand = 0; hand < 2; ++hand)
                if (s_pending[hand] == id) s_pending[hand] = 0;
        }
        break;
    }
    }
}

void gevrTacticalLootTick(void)
{
    if (!gevrTacticalLootEnabled()) return;
    const uint64_t now = sysGetMicroseconds();
    if (now - s_lastTick < LOOT_INTERVAL_US) return;
    s_lastTick = now;
    for (int i = 0; i < MAX_WORLD_LOOT; ++i) {
        WorldLoot *entry = &s_world[i];
        if (!entry->loot.id) continue;
        if (!entry->presented) entry->presented = gevrTacticalLootPutWorld(&entry->loot);
        if (!entry->simulatedHere) continue;
        GevrSharedLoot updated = entry->loot;
        int moving;
        if (!gevrTacticalLootSample(&updated, &moving)) continue;
        float delta = 0, turn = 0;
        for (int axis = 0; axis < 3; ++axis) {
            delta += fabsf(updated.position[axis] - entry->loot.position[axis]);
            if (updated.kind == GEVR_LOOT_GUN)
                for (int col = 0; col < 3; ++col)
                    turn += fabsf(updated.orientation[axis][col] - entry->loot.orientation[axis][col]);
        }
        if (delta > .25f || turn > .015f || entry->moving != moving) {
            entry->loot = updated;
            ++entry->motion;
            sendMotion(entry, moving);
            entry->moving = moving;
        }
    }
}
