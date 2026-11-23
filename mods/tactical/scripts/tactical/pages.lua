-- The in-game screen's pages (mod.ui): Loadout and Progress. Two of the
-- four pages every mod shares. The Progress page asks the one-time unlock
-- question until it's answered.
local catalog = require "tactical.catalog"
local progress = require "tactical.progress"
local magazines = require "tactical.magazines"
local state = require "tactical.state"
local firing = require "tactical.firing"

local pages = {}

local LOADOUT, PROGRESS = "tac_loadout", "tac_progress"
local APPLY_WINDOW = 30        -- seconds into a mission the loadout can still be swapped

local view = { category = 1, primary = 1, pistol = 1, gunCat = 1, gun = 1, note = nil }

local function label(def)
  return (progress.owned(def.key) and "" or "* ") .. def.name
end

local function primaryList()
  return catalog.categories[catalog.primaryCategories[view.category]]
end

local function pistolList() return catalog.categories.Pistols end

local function capChoices(key)
  local names, caps = {}, {}
  local def = catalog.byKey[key]
  if not def then return { "-", "-" }, { 0, 0 } end
  if catalog.noMagazine[key] then return { "Loads by hand", "-" }, { 0, 0 } end
  for _, cap in ipairs(catalog.capacities(def)) do
    if progress.magazineAllowed(key, cap) then
      caps[#caps + 1] = cap
      names[#names + 1] = cap .. " rounds"
    end
  end
  if #names < 2 then names[#names + 1] = "(more by clears)"; caps[#caps + 1] = 0 end
  return names, caps
end

local function indexOf(list, pred)
  for i, v in ipairs(list) do if pred(v) then return i end end
  return 1
end

local function selectedPrimary()
  return primaryList()[view.primary - 1]   -- row 1 is "None"
end

local function selectedPistol()
  return pistolList()[view.pistol]
end

-- Point the view at the saved loadout.
local function syncView()
  local lo = progress.loadout()
  local p = catalog.byKey[lo.primary]
  if p then
    for i, name in ipairs(catalog.primaryCategories) do
      if name == p.category then view.category = i end
    end
    view.primary = indexOf(primaryList(), function(d) return d.key == p.key end) + 1
  else
    view.primary = 1
  end
  view.pistol = indexOf(pistolList(), function(d) return d.key == lo.pistol end)
end

function pages.loadout()
  local cats = {}
  for i, c in ipairs(catalog.primaryCategories) do cats[i] = c end
  local prim = { "None" }
  for _, d in ipairs(primaryList()) do prim[#prim + 1] = label(d) end
  local pist = {}
  for _, d in ipairs(pistolList()) do pist[#pist + 1] = label(d) end
  local p, s = selectedPrimary(), selectedPistol()
  local pCaps, pCapVals = capChoices(p and p.key)
  local sCaps, sCapVals = capChoices(s and s.key)
  local pCap = p and progress.magazineChoice(p.key)
  local sCap = s and progress.magazineChoice(s.key)
  local modes = p and catalog.modes(p) or {}
  local modeNames = {}
  for i, m in ipairs(modes) do modeNames[i] = m:sub(1, 1):upper() .. m:sub(2) end
  local text = view.note
  if not text then
    local shown = p or s
    if shown and not progress.owned(shown.key) then
      text = progress.requirement(shown.key)
    elseif p then
      local mags, rounds = magazines.summary(p)
      text = string.format("%s: %s. Rig %d mags, %d rounds. Apply in the first %ds of a mission. * = locked.",
        p.name, table.concat(modeNames, "/"), mags, rounds, APPLY_WINDOW)
    else
      text = "Choose a primary and a pistol. They're issued at the next mission's start. * = locked."
    end
  end
  local blockedApply = (state.stage < 0 or state.clock > APPLY_WINDOW) and "Next mission" or false
  local rows = {
    { id = "category", label = "Primary type", kind = "choice", choices = cats, value = view.category },
    { id = "primary", label = "Primary", kind = "choice", choices = prim, value = view.primary },
    { id = "pmag", label = "Primary magazine", kind = "choice", choices = pCaps,
      value = indexOf(pCapVals, function(c) return c == pCap end), blocked = p == nil and "No primary" or false },
    { id = "pistol", label = "Pistol", kind = "choice", choices = pist, value = view.pistol },
    { id = "smag", label = "Pistol magazine", kind = "choice", choices = sCaps,
      value = indexOf(sCapVals, function(c) return c == sCap end), blocked = s == nil and "No pistol" or false },
  }
  if #modeNames >= 2 then
    rows[#rows + 1] = { id = "mode", label = "Fire mode", kind = "choice", choices = modeNames,
      value = indexOf(modes, function(m) return m == progress.firemode(p.key) end),
      tip = "Or grip at your chest with the off hand to switch the held gun's mode." }
  end
  local shown = p or s
  local canBuy = shown and not progress.owned(shown.key) and progress.reserve() > 0
    and progress.totalClears() >= shown.reserveAt
  mod.ui.page(LOADOUT, {
    title = "Loadout", text = text:sub(1, 160), rows = rows,
    buttons = {
      { id = "save", label = "Save" },
      { id = "apply", label = "Apply now", blocked = blockedApply },
      { id = "buy", label = "Use reserve", blocked = (not canBuy) and "Not available" or false },
    },
  })
  view.capVals = { pmag = pCapVals, smag = sCapVals }
end

local function gunPickList()
  local list = {}
  local cat = catalog.primaryCategories[view.gunCat] or "Pistols"
  for _, d in ipairs(catalog.categories[cat]) do list[#list + 1] = d end
  return list, cat
end

function pages.progress()
  if progress.mode() == progress.UNSET then
    mod.ui.page(PROGRESS, {
      title = "Tactical",
      text = "Unlock every gun and magazine for this save? You're asked once only. No: start with the PPK and earn the rest.",
      rows = {},
      buttons = { { id = "all", label = "Yes, unlock" }, { id = "earn", label = "No, progress" } },
    })
    return
  end
  local cats = {}
  for i, c in ipairs(catalog.primaryCategories) do cats[i] = c end
  cats[#cats + 1] = "Pistols"
  local list = gunPickList()
  local names = {}
  for i, d in ipairs(list) do names[i] = label(d) end
  view.gun = math.min(view.gun, #list)
  local d = list[view.gun]
  local st = progress.gunStats(d.key)
  local t = progress.totals()
  local acc = st.shots > 0 and math.floor(100 * st.hits / st.shots + 0.5) or 0
  local text = string.format("%s: %d kills, %d HS, %d%% hits, %d clears. All: %d kills, %d missions, %d/42 guns, reserve %d.",
    d.name, st.kills, st.headshots, acc, st.clears, t.kills, progress.missions(), progress.unlockedCount(),
    progress.reserve())
  mod.ui.page(PROGRESS, {
    title = "Progress", text = text:sub(1, 160),
    rows = {
      { id = "cat", label = "Guns", kind = "choice", choices = cats, value = view.gunCat },
      { id = "gun", label = "Gun", kind = "choice", choices = names, value = view.gun,
        tip = progress.requirement(d.key):sub(1, 160) },
    },
    buttons = {},
  })
end

function pages.refresh()
  pages.loadout()
  pages.progress()
end

function pages.init()
  syncView()
  pages.refresh()
end

-- The loadout's choices are saved and, early in a mission, issued at once.
local function save()
  local p, s = selectedPrimary(), selectedPistol()
  local pk = p and progress.owned(p.key) and p.key or -1
  local sk = s and progress.owned(s.key) and s.key or -1
  progress.setLoadout(pk, sk)
  if (p and pk < 0) or (s and sk < 0) then
    view.note = "Saved, without the locked gun (*)."
  else
    view.note = "Saved."
  end
end

pages.onApply = nil   -- set by main.lua: issue the kit again

function pages.change(page, row, value)
  view.note = nil
  if page == LOADOUT then
    if row == "category" then view.category, view.primary = value, 1
    elseif row == "primary" then view.primary = value
    elseif row == "pistol" then view.pistol = value
    elseif row == "pmag" or row == "smag" then
      local def = row == "pmag" and selectedPrimary() or selectedPistol()
      local cap = view.capVals[row][value]
      if def and cap and cap > 0 then progress.setMagazine(def.key, cap) end
    elseif row == "mode" then
      local p = selectedPrimary()
      if p then progress.setFiremode(p.key, catalog.modes(p)[value]) end
    end
    pages.loadout()
  elseif page == PROGRESS then
    if row == "cat" then view.gunCat, view.gun = value, 1
    elseif row == "gun" then view.gun = value end
    pages.progress()
  end
end

function pages.click(page, button)
  if page == PROGRESS and (button == "all" or button == "earn") then
    progress.answer(button == "all")
    pages.refresh()
    return
  end
  if page ~= LOADOUT then return end
  view.note = nil
  if button == "save" then
    save()
  elseif button == "apply" then
    save()
    if pages.onApply then pages.onApply() end
    view.note = "Issued."
  elseif button == "buy" then
    local d = selectedPrimary() or selectedPistol()
    if d and progress.requisition(d.key) then view.note = d.name .. " bought with reserve." end
  end
  pages.refresh()
end

-- Opened at a mission's start while the question is unanswered.
function pages.ask()
  if progress.mode() == progress.UNSET then
    pages.progress()
    mod.ui.open(PROGRESS)
  end
end

function pages.debrief(result)
  local parts = {}
  for _, key in ipairs(result.guns) do parts[#parts + 1] = catalog.byKey[key].name end
  for _, m in ipairs(result.magazines) do
    parts[#parts + 1] = catalog.byKey[m.key].name .. " " .. m.cap .. "-rd mag"
  end
  if result.reserve > 0 then parts[#parts + 1] = "+1 reserve" end
  if #parts > 0 then
    view.note = ("Unlocked: " .. table.concat(parts, ", ")):sub(1, 160)
    mod.log(view.note)
  end
end

pages.LOADOUT, pages.PROGRESS = LOADOUT, PROGRESS
return pages
