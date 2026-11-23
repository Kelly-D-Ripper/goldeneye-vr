-- The gun list (guns.lua, generated) indexed the ways the other modules need.
local guns = require "tactical.guns"

local catalog = { guns = guns, byKey = {}, byName = {}, byItem = {}, categories = {} }

-- Spare-round types of the cartridge's guns (mod.ammo names). The API has no
-- read for an item's ammo type, so this mirrors the cartridge's weapon table.
catalog.ammoOf = {
  wppk = "9mm", wppksil = "9mm", tt33 = "9mm", skorpion = "9mm_2", ak47 = "rifle",
  uzi = "9mm", mp5k = "9mm", mp5ksil = "9mm", spectre = "9mm", m16 = "rifle",
  fnp90 = "9mm", shotgun = "shotgun", autoshot = "shotgun", sniperrifle = "rifle",
  ruger = "magnum", goldengun = "goldengun", silverwppk = "9mm", goldwppk = "9mm",
  grenadelaunch = "grenaderound", rocketlaunch = "rockets",
}

-- The cartridge gun a looted pickup of that item becomes when no guard's gun
-- explains it: the 0.18.4 build's mapping for the original guns.
catalog.defaultFor = {
  wppk = 1, wppksil = 1, tt33 = 5, skorpion = 6, ak47 = 4, uzi = 9, mp5k = 3,
  mp5ksil = 18, spectre = 2, m16 = 16, fnp90 = 21, autoshot = 27, sniperrifle = 7,
  ruger = 25, grenadelaunch = 26, rocketlaunch = 28,
}

catalog.primaryCategories = { "Rifles", "SMGs I", "SMGs II", "Heavy" }
catalog.noMagazine = { [26] = true, [28] = true }   -- M32A1, RPG-7V2: the cartridge loads them
catalog.rigSlots = { [15] = 1, [28] = 2 }            -- PKM boxes, RPG rounds (gevr_tactical_session.h)

for _, g in ipairs(guns) do
  g.itemId = mod.items[g.item]
  catalog.byKey[g.key] = g
  catalog.byName[g.name] = g
  local list = catalog.byItem[g.itemId]
  if not list then list = {}; catalog.byItem[g.itemId] = list end
  list[#list + 1] = g
  local cat = catalog.categories[g.category]
  if not cat then cat = {}; catalog.categories[g.category] = cat end
  cat[#cat + 1] = g
end

-- Item ids back to names, for the ammo table and logs.
catalog.itemName = {}
for name, id in pairs(mod.items) do catalog.itemName[id] = name end

function catalog.ammo(itemId)
  return catalog.ammoOf[catalog.itemName[itemId] or ""]
end

function catalog.isGunItem(itemId)
  return catalog.byItem[itemId] ~= nil
end

function catalog.defaultKey(itemId)
  return catalog.defaultFor[catalog.itemName[itemId] or ""]
end

-- Fire modes the gun has, in the order the selector cycles them.
function catalog.modes(g)
  local out = {}
  if g.modes & 1 ~= 0 then out[#out + 1] = "semi" end
  if g.modes & 2 ~= 0 then out[#out + 1] = "burst" end
  if g.modes & 4 ~= 0 then out[#out + 1] = "auto" end
  return out
end

-- gevr_tactical_stats.c gevrFireMode: auto when it has one, else semi, else burst.
function catalog.defaultMode(g)
  if g.modes & 4 ~= 0 then return "auto" end
  if g.modes & 1 ~= 0 then return "semi" end
  return "burst"
end

function catalog.capacities(g)
  local caps = {}
  for cap in pairs(g.magazines) do caps[#caps + 1] = cap end
  table.sort(caps)
  return caps
end

function catalog.pouches(g)
  if catalog.rigSlots[g.key] then return catalog.rigSlots[g.key] end
  return g.slot == "pistol" and 2 or 3
end

return catalog
