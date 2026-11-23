-- GoldenEye Tactical as a mod pack (mod API v1).
--
-- Each module keeps one system; this file wires the game's events to them,
-- in one handler per event so their order is plain.
local catalog = require "tactical.catalog"
local state = require "tactical.state"
local progress = require "tactical.progress"
local arsenal = require "tactical.arsenal"
local magazines = require "tactical.magazines"
local firing = require "tactical.firing"
local guards = require "tactical.guards"
local pages = require "tactical.pages"
local coop = require "tactical.coop"
local util = require "tactical.util"

local CHEST_SWITCH = "chest"   -- the off hand's grip here cycles the gun hand's fire mode
local completed = false

progress.load()
coop.init()

mod.on("campaign_enter", function()
  pages.init()
end)

mod.on("mission_prepare", function(stage)
  state.reset(stage)
  coop.missionReset()
  completed = false
end)

local function issueKit()
  arsenal.applyKit(state.stage)
  magazines.begin()
  for _, hand in ipairs(arsenal.HANDS) do
    local item = util.try(mod.weapon.held, hand)
    if item and state.carried[item] then
      arsenal.apply(item, state.carried[item])
      magazines.drawn(hand, item)
    end
  end
end

mod.on("mission_start", function(stage)
  issueKit()
  pages.refresh()
  pages.ask()
end)

pages.onApply = function()
  if state.stage >= 0 then issueKit() end
end

mod.on("tick", function(dt)
  state.clock = state.clock + dt
  state.ticks = state.ticks + 1
  for _, hand in ipairs(arsenal.HANDS) do
    local item = util.try(mod.weapon.held, hand)
    if item and catalog.isGunItem(item) and util.try(mod.inventory.has, item) then
      arsenal.carry(item)
    end
    arsenal.noteHeld(hand, item)
    if item then magazines.observe(hand, item) end
  end
  arsenal.releaseUnheld()
  magazines.sync()
  guards.tick()
  coop.shareGuns()
end)

-- weapons --------------------------------------------------------------

mod.on("weapon_fire_attempt", function(hand, item)
  return firing.attempt(hand, item)
end)

mod.on("weapon_fire", function(hand, item, loaded)
  local key = state.carried[item]
  if key then
    progress.shot(key)
    if not catalog.noMagazine[key] then arsenal.gun(key).loaded = loaded end
  end
end)

mod.on("reload_start", function(hand, item)
  magazines.reloadStart(hand, item)
end)

mod.on("reload_end", function(hand, item, loaded)
  magazines.reloadEnd(hand, item)
end)

mod.on("weapon_drawn", function(hand, item)
  local key = arsenal.carry(item)
  arsenal.noteHeld(hand, item)
  if key then
    magazines.trackAmmo(item)
    arsenal.apply(item, key)
    magazines.drawn(hand, item)
  end
  arsenal.releaseUnheld()
end)

-- items ----------------------------------------------------------------

mod.on("item_pickup", function(kind, id, rounds)
  if kind == "weapon" then
    local key = arsenal.carry(id)
    if key then magazines.trackAmmo(id) end
  end
  -- rounds reach the reserve; the next sync counts them as loose
end)

-- hands ----------------------------------------------------------------

mod.on("grip", function(hand, anchor, taken)
  if hand == "left" and anchor == CHEST_SWITCH and not taken then
    firing.cycleMode("right")
  end
end)

-- guards ---------------------------------------------------------------

mod.on("guard_spawning", function(g)
  return guards.spawning(g)
end)

mod.on("guard_alerted", function(id, how, player)
  guards.alerted(id, how)
end)

mod.on("guard_killed", function(id, how, item, player)
  guards.killed(id)
  if how ~= "shot" or item == nil then return end
  if player == nil then return end
  if not coop.tellStat(player, "kill", item, false) then
    local key = state.carried[item]
    if key then progress.kill(key) end
  end
end)

-- damage ---------------------------------------------------------------

mod.on("before_damage", function(d)
  return firing.beforeDamage(d)
end)

mod.on("damage_applied", function(d)
  if d.target ~= "guard" or d.by == nil or d.item == nil then return end
  local head = d.part == "head"
  if not coop.tellStat(d.by, "hit", d.item, head) then
    local key = state.carried[d.item]
    if key then progress.hit(key, head) end
  end
end)

-- the mission's end ----------------------------------------------------

mod.on("mission_end", function(stage, result)
  if result == "complete" and not completed then
    completed = true
    local res = progress.missionComplete(stage, state.ownedAtStart, arsenal.broughtHome())
    pages.debrief(res)
  else
    progress.missionFailed()
  end
  arsenal.releaseUnheld()
  pages.refresh()
end)

mod.on("player_died", function(slot)
  -- dying loses what was picked up: nothing is brought home
  state.held = {}
  state.lastPrimary = nil
end)

-- the screen -----------------------------------------------------------

mod.on("ui_change", function(page, row, value)
  pages.change(page, row, value)
end)

mod.on("ui_click", function(page, button)
  pages.click(page, button)
end)

mod.log(string.format("Tactical %s: %d guns, progression mode %d", mod.version, #catalog.guns, progress.mode()))
