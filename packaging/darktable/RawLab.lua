-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Ayeda Okambawa
-- RawLab Darktable integration, protocol v1

local dt = require "darktable"
local du = require "lib/dtutils"
local df = require "lib/dtutils.file"
local dsys = require "lib/dtutils.system"
du.check_min_api_version("7.0.0", "RawLab")

local _ = dt.gettext.gettext
local MODULE_ID, EXECUTABLE_NAME = "RawLab", "rawlab"
local separator = dt.configuration.running_os == "windows" and "\\" or "/"
local state = { installed = false, event = false, running = false }
local stack, apply_first

local function exists(path)
  local file = io.open(path, "rb")
  if file then file:close(); return true end
  return false
end

local function executable()
  local configured = df.get_executable_path_preference(EXECUTABLE_NAME)
  if not configured or configured == "" then return nil end
  return df.check_if_bin_exists(EXECUTABLE_NAME)
end

local function output_for(image, occupied)
  local base = image.filename:match("(.+)%.[^%.]+$") or image.filename
  local stem, index = image.path .. separator .. base .. "-rawlab", 1
  local candidate = stem .. ".dng"
  while occupied[candidate] or exists(candidate) or exists(candidate .. ".xmp") do
    candidate = string.format("%s_%d.dng", stem, index); index = index + 1
  end
  occupied[candidate] = true
  return candidate
end

local function copy_metadata(source, imported)
  imported.rating = source.rating
  imported.red, imported.yellow, imported.green = source.red, source.yellow, source.green
  imported.blue, imported.purple = source.blue, source.purple
  for _, tag in ipairs(dt.tags.get_tags(imported)) do
    if tag.name:sub(1, 10) ~= "darktable|" then dt.tags.detach(tag, imported) end
  end
  for _, tag in ipairs(dt.tags.get_tags(source)) do
    if tag.name:sub(1, 10) ~= "darktable|" then dt.tags.attach(tag, imported) end
  end
end

local function hex(value)
  return (value:gsub(".", function(character) return string.format("%02x", string.byte(character)) end))
end

local function unhex(value)
  if #value % 2 ~= 0 or value:find("[^0-9a-f]") then error("Invalid RawLab protocol path") end
  return (value:gsub("..", function(pair) return string.char(tonumber(pair, 16)) end))
end

local function write(path, contents)
  local file, open_error = io.open(path, "wb")
  if not file then error(open_error) end
  local ok, write_error = file:write(contents)
  local closed, close_error = file:close()
  if not ok or not closed then error(write_error or close_error) end
end

local function read_receipt(path, allowed, expected)
  local file = io.open(path, "rb")
  if not file then return nil end
  local size = file:seek("end")
  if not size or size > 4 * 1024 * 1024 then file:close(); error("RawLab receipt is too large") end
  file:seek("set", 0)
  local contents = file:read("*a"); file:close()
  if contents:sub(-1) ~= "\n" then error("RawLab receipt is missing its final newline") end
  local lines = {}
  for line in contents:gmatch("([^\n]+)\n") do lines[#lines + 1] = line end
  if lines[1] ~= "rawlab-results-v1" or (lines[2] ~= "complete" and lines[2] ~= "partial") then
    error("Unsupported RawLab receipt")
  end
  local records, seen = {}, {}
  for index = 3, #lines do
    if #records >= expected then error("RawLab receipt has too many records") end
    local source_hex, output_hex = lines[index]:match("^([0-9a-f]+)\t([0-9a-f]+)$")
    if not source_hex then error("Invalid RawLab receipt record") end
    local source, output = unhex(source_hex), unhex(output_hex)
    if allowed[output] ~= source or seen[output] then error("RawLab receipt does not match its request") end
    seen[output] = true; records[#records + 1] = { source = source, output = output }
  end
  return records, lines[2]
end

local function import_results(receipt, allowed, expected)
  local records, status = read_receipt(receipt, allowed, expected)
  if not records then return false end
  local database = {}
  for _, image in ipairs(dt.database) do database[image.path .. separator .. image.filename] = image end
  local imported_all = true
  for _, record in ipairs(records) do
    if not database[record.output] then
      local source = database[record.source]
      if source and exists(record.output) then
        local imported = dt.database.import(record.output)
        if imported then
          imported:reset(); imported:group_with(source); copy_metadata(source, imported)
          database[record.output] = imported
        else imported_all = false end
      else imported_all = false end
    end
  end
  return status == "complete" and #records == expected and imported_all
end

local function run_batch(images, occupied)
  local binary = executable()
  if not binary then error("Select the RawLab desktop executable first") end
  local request, records, allowed = os.tmpname(), { "rawlab-batch-v1\n" }, {}
  for _, image in ipairs(images) do
    local source = image.path .. separator .. image.filename
    local output = output_for(image, occupied)
    allowed[output] = source
    records[#records + 1] = hex(source) .. "\t" .. hex(output) .. "\n"
  end
  write(request, table.concat(records)); os.remove(request .. ".results.cancel")
  local job = dt.gui.create_job(_("RawLab batch - progress shown in RawLab"), true, function(progress)
    local ok, failure = pcall(write, request .. ".results.cancel", "cancel\n")
    if not ok then dt.print_error(tostring(failure)) end
    progress.valid = false
  end)
  local ran, result = pcall(dsys.external_command, binary .. " --external-batch " .. df.sanitize_filename(request))
  if job.valid then job.valid = false end
  local imported, complete = pcall(import_results, request .. ".results", allowed, #images)
  os.remove(request .. ".results.cancel")
  if ran and imported then os.remove(request); os.remove(request .. ".results") end
  if not ran then error(result) end
  if not imported then
    dt.print_error("RawLab receipt retained at " .. request .. ".results")
    error(complete)
  end
  if complete then dt.print(_("RawLab batch complete; outputs imported"))
  else dt.print(_("RawLab batch stopped; completed outputs imported")) end
end

local function run_single(image, occupied)
  local binary = executable()
  if not binary then error("Select the RawLab desktop executable first") end
  local source = image.path .. separator .. image.filename
  local output = output_for(image, occupied)
  local command = binary .. " --external-edit " .. df.sanitize_filename(source)
    .. " --output " .. df.sanitize_filename(output)
  local result = dsys.external_command(command)
  if result ~= 0 then error("RawLab exited with status " .. tostring(result)) end
  if not exists(output) then error("RawLab did not create " .. output) end
  local imported = dt.database.import(output)
  if not imported then error("Darktable could not import " .. output) end
  imported:reset(); imported:group_with(image); copy_metadata(image, imported)
end

local function edit_selected()
  if state.running then dt.print(_("A RawLab operation is already running")); return end
  local images = {}
  for _, image in ipairs(dt.gui.selection()) do images[#images + 1] = image end
  if #images == 0 then dt.print(_("Select at least one image")); return end
  local occupied = {}
  for _, image in ipairs(dt.database) do occupied[image.path .. separator .. image.filename] = true end
  state.running = true
  local ok, failure = pcall(function()
    if apply_first.value and #images > 1 then run_batch(images, occupied)
    else for _, image in ipairs(images) do run_single(image, occupied) end end
  end)
  state.running = false
  if not ok then dt.print_error(tostring(failure)); dt.print(_("RawLab failed; see the log")) end
end

apply_first = dt.new_widget("check_button"){
  label = _("Edit first image, apply settings to all"), value = false,
  tooltip = _("Adjust the first image in RawLab, then export every selected image with those settings"),
}
local options = dt.new_widget("box"){
  orientation = "vertical",
  dt.new_widget("section_label"){ label = _("Edit RAW files with RawLab") },
  apply_first,
  dt.new_widget("button"){ label = _("Edit with RawLab"), clicked_callback = edit_selected },
}
local chooser = dt.new_widget("file_chooser_button"){
  title = _("Select RawLab desktop executable or launcher"),
  value = df.get_executable_path_preference(EXECUTABLE_NAME), is_directory = false,
}
local setup = dt.new_widget("box"){
  orientation = "vertical", chooser,
  dt.new_widget("button"){
    label = _("Use executable"), clicked_callback = function()
      df.set_executable_path_preference(EXECUTABLE_NAME, chooser.value or "")
      stack.active = executable() and 1 or 2
    end,
  },
}
stack = dt.new_widget("stack"){ options, setup }
stack.active = executable() and 1 or 2

local function install()
  if state.installed then return end
  dt.register_lib(MODULE_ID, _("RawLab"), true, false,
    { [dt.gui.views.lighttable] = { "DT_UI_CONTAINER_PANEL_RIGHT_CENTER", 100 } },
    dt.new_widget("box"){ orientation = "vertical", stack })
  state.installed = true
end
if dt.gui.current_view().id == "lighttable" then install()
else
  dt.register_event(MODULE_ID, "view-changed", function(_, _, view) if view.id == "lighttable" then install() end end)
  state.event = true
end

local script_data = { metadata = { name = "RawLab", purpose = _("Edit RAW files with RawLab"), author = "Ayeda Okambawa" } }
script_data.destroy = function() if dt.gui.libs[MODULE_ID] then dt.gui.libs[MODULE_ID].visible = false end end
script_data.restart = function() if dt.gui.libs[MODULE_ID] then dt.gui.libs[MODULE_ID].visible = true end end
script_data.destroy_method = "hide"
script_data.show = script_data.restart
return script_data
