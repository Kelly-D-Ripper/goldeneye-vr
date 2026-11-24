-- One mission's state, shared by the modules. Reset as each mission loads.
local state = {}

function state.reset(stage)
  state.stage = stage
  state.clock = 0              -- seconds of play this mission (tick's dt summed)
  state.ticks = 0              -- ticks of play this mission
  state.carried = {}           -- cartridge item id -> Tactical gun key
  state.guns = {}              -- key -> { loaded, cap, chamber }
  state.held = {}              -- "right"/"left" -> item id held, as last seen
  state.lastPrimary = nil      -- the primary last put away: the back slot
  state.ownedAtStart = {}      -- keys owned as the mission began
  state.drops = {}             -- legacy only: guard guns by item id, newest last
  state.pickupDrops = {}       -- mission pickup id -> { item, key }
  state.rig = { primary = {}, pistol = {} }   -- magazines in pouches: { family, cap, rounds }
  state.loose = {}             -- ammo type -> rounds not yet in a magazine
  state.pinned = {}            -- ammo type -> the spare count the game was last given
  state.reloading = {}         -- ammo type -> tick until which a reload's rounds aren't loose
  state.applied = {}           -- item id -> key whose stats the item carries now
  state.original = {}          -- item id -> the item's own stats
  state.pull = { right = { last = -1, count = 0 }, left = { last = -1, count = 0 } }
  state.guards = {}            -- guard id -> { key, armoured }
  state.reinforcements = { ids = {}, count = 0, after = 0, wave = nil, sequence = 0 }
  state.peers = {}             -- co-op slot -> item id -> key (the host's view of each player's guns)
  state.kitApplied = false
end

state.reset(-1)
return state
