-- Small helpers shared by the Tactical modules.
local util = {}

local M32 = 0xffffffff

-- The 0.18.4 build's guard weapon draw (gevr_tactical_loadouts.c
-- campaignWeaponDraw): the same mission and guard always give the same
-- roll, on every headset and after every restart.
function util.draw(stage, id)
  local d = ((stage & 0xff) << 16) | (id & 0xffff)
  d = ((d ~ (d >> 16)) * 0x7feb352d) & M32
  d = ((d ~ (d >> 15)) * 0x846ca68b) & M32
  return (d ~ (d >> 16)) & M32
end

function util.clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

-- A call that may fail when the player isn't in a stage (between missions,
-- dead, a spectator): nil instead of an error that would stop the mod.
function util.try(f, ...)
  local ok, a, b, c = pcall(f, ...)
  if ok then return a, b, c end
  return nil
end

function util.copy(t)
  local c = {}
  for k, v in pairs(t) do c[k] = v end
  return c
end

function util.sortedKeys(t)
  local keys = {}
  for k in pairs(t) do keys[#keys + 1] = k end
  table.sort(keys)
  return keys
end

-- "a=1;b=2" style lines, written whole by mod.save.
function util.splitFields(line, sep)
  local out = {}
  for field in string.gmatch(line, "[^" .. sep .. "]+") do out[#out + 1] = field end
  return out
end

return util
