-- Tactical's guns as the native build draws them: the build's body through
-- mod.weapon.model (with its native muzzle) and every other part through
-- mod.attach on the gun, at the frames the native renderer uses
-- (assemblies.lua, generated from the native source).
--
-- Attachments ride "the gun that hand holds", so a hand's parts come off
-- at every draw and the new gun's go on. The magazine hides while a reload
-- has it out of the gun.
local catalog = require "tactical.catalog"
local progress = require "tactical.progress"
local state = require "tactical.state"
local util = require "tactical.util"
local assemblies = require "tactical.assemblies"
local arsenal = require "tactical.arsenal"
local opticAssets = mod.attach and mod.attach.optic and util.try(require, "tactical.optic_assets") or {}

local looks = {}

local attached = { right = {}, left = {} }   -- hand -> { ids }
local moving = { right = {}, left = {} }     -- hand -> { { id, motion } } parts that move with the action
local drawnBuild = { right = nil, left = nil } -- hand -> { key, build } for re-timing on a mode change
local magazine = { right = nil, left = nil } -- hand -> the magazine's attachment id
local shown = { right = nil, left = nil }    -- hand -> "item:key:preset" drawn now
local failed = {}

local function clear(hand)
  for _, id in ipairs(attached[hand]) do util.try(mod.attach.remove, id) end
  attached[hand], magazine[hand], shown[hand] = {}, nil, nil
  moving[hand], drawnBuild[hand] = {}, nil
end

-- The native shot's return (gevr_tactical_session.c gevrTacticalShotFired): the
-- base ticks, cut to the build's interval between shots outside semi (never
-- under 3), then the action is fully open on the shot's own tick and closes over
-- the rest, eased as gevrTacticalShotTravel's open * (0.4 + 0.6 * open).
local function actionTiming(key, b)
  local duration = b.action.duration
  local rpm = b.stats and b.stats.rpm or 0
  if progress.firemode(key) ~= "semi" and rpm > 0 then
    duration = math.floor(math.max(3, math.min(duration, 3600 / rpm)))
  end
  return { open = 1, close = duration - 1, ease = 0.4 }
end

-- Each moving part along the native slide (gearPartMatrix: back along the
-- gun by gevrTacticalActionShift of the stroke); a charging handle that only
-- the rack moves has no shot timing.
local function setMotion(hand, key, b)
  if not (mod.attach.action and b.action) then return end
  local timing = actionTiming(key, b)
  for _, m in ipairs(moving[hand]) do
    local spec = { travel = { 0, 0, -b.action.travel } }
    if m.motion == 2 then
      spec.handle = true
    else
      spec.open, spec.close, spec.ease = timing.open, timing.close, timing.ease
    end
    util.try(mod.attach.action, m.id, spec)
  end
end

local function addPart(hand, part)
  local attachment = { on = "gun", hand = hand, frame = part.frame }
  local optic = opticAssets[part.model]
  if optic then
    attachment.model = optic.model
    local id = util.try(mod.attach.add, attachment)
    if id then
      if util.try(mod.attach.optic, id, { profile = optic.profile, folded = optic.folded }) then return id end
      util.try(mod.attach.remove, id)
    end
    if not failed[optic.profile] then mod.log("couldn't load native optic " .. optic.profile) end
    failed[optic.profile] = true
  end
  attachment.model = part.model
  return util.try(mod.attach.add, attachment)
end

-- The build a carried gun has: the loadout's choice for the kit's guns, a
-- guard's roster preset for loot (arsenal.lua keeps it per cartridge gun).
function looks.build(key, preset)
  return arsenal.build(key, preset)
end

-- weapon_drawn: dress the hand's gun.
function looks.drawn(hand, itemId)
  if not mod.attach or not mod.weapon.model then return end   -- a build before C10 and C1
  local key = itemId and state.carried[itemId]
  local preset = itemId and arsenal.preset(itemId)
  local b = key and looks.build(key, preset)
  local tag = b and (itemId .. ":" .. key .. ":" .. preset) or nil
  if tag and tag == shown[hand] then return end
  clear(hand)
  if not b then return end
  local body = b.parts[1]
  local look = { frame = body.frame, muzzle = b.muzzle }
  -- native's action doesn't lock back on an empty magazine (gevrTacticalShotTravel)
  if mod.attach.action then look.holdOpen = false end
  local ok = util.try(mod.weapon.model, hand, itemId, body.model, look)
  if not ok then
    if not failed[body.model] then mod.log("no pack body " .. body.model .. " for " .. catalog.byKey[key].name) end
    failed[body.model] = true
    return
  end
  for i = 2, #b.parts do
    local p = b.parts[i]
    local id = addPart(hand, p)
    if id then
      attached[hand][#attached[hand] + 1] = id
      if p.path:match("mod_magazine$") then magazine[hand] = id end
      if p.motion then moving[hand][#moving[hand] + 1] = { id = id, motion = p.motion } end
    elseif not failed[p.model] then
      failed[p.model] = true
      mod.log("part " .. p.model .. " (" .. p.path .. ") didn't attach")
    end
  end
  setMotion(hand, key, b)
  drawnBuild[hand] = { key = key, build = b }
  shown[hand] = tag
end

-- A fire mode change: the moving parts take the new mode's return.
function looks.retime(hand)
  local d = drawnBuild[hand]
  if d then setMotion(hand, d.key, d.build) end
end

function looks.reloadStart(hand)
  if magazine[hand] then util.try(mod.attach.show, magazine[hand], false) end
end

function looks.reloadEnd(hand)
  if magazine[hand] then util.try(mod.attach.show, magazine[hand], true) end
end

-- A mission's start: the game dropped every attachment and model choice.
function looks.reset()
  attached = { right = {}, left = {} }
  moving, drawnBuild = { right = {}, left = {} }, {}
  magazine, shown = {}, {}
end

-- The build's choices for the loadout page: "-" first, then the native presets.
function looks.presets(key)
  local names = {}
  local builds = assemblies[key]
  if not builds then return names end
  for name in pairs(builds) do if name ~= "-" then names[#names + 1] = name end end
  table.sort(names)
  table.insert(names, 1, "-")
  return names
end

return looks
