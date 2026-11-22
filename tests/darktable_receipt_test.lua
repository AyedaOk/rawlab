local script, root = assert(arg[1]), assert(arg[2])
local function write(path, text)
  local file = assert(io.open(path, "wb"))
  assert(file:write(text)); assert(file:close())
end
local function exists(path)
  local file = io.open(path, "rb")
  if file then file:close(); return true end
  return false
end
local function unhex(value)
  return (value:gsub("..", function(pair) return string.char(tonumber(pair, 16)) end))
end

for _, mode in ipairs({"success", "import-fails", "partial", "nonzero", "throws"}) do
  local request = root .. "-" .. mode
  local outputs, messages = {}, {}
  local clicked
  local images = {
    {path=root, filename="one.raw", rating=3},
    {path=root, filename="two.raw", rating=4},
  }
  -- Use a real directory for the generated outputs without creating source files.
  for index, image in ipairs(images) do
    image.path = root:match("^(.*)/")
    image.filename = request:match("([^/]+)$") .. "-" .. index .. ".raw"
  end
  local database = {images[1], images[2]}
  database.import = function()
    if mode == "import-fails" then return nil end
    return {reset=function() end, group_with=function() end}
  end
  local dt = {
    configuration={running_os="linux"}, gettext={gettext=function(text) return text end},
    database=database, tags={get_tags=function() return {} end},
    print=function(text) messages[#messages+1]=text end,
    print_error=function(text) messages[#messages+1]=text end,
    register_lib=function() end, register_event=function() end,
    gui={selection=function() return images end,
      create_job=function() return {valid=true} end,
      current_view=function() return {id="lighttable"} end, views={lighttable={}}, libs={}},
    new_widget=function(kind) return function(widget)
      if kind == "check_button" then widget.value=true end
      if widget.label == "Edit with RawLab" then clicked=widget.clicked_callback end
      return widget
    end end,
  }
  package.loaded.darktable=dt
  package.loaded["lib/dtutils"]={check_min_api_version=function() end}
  package.loaded["lib/dtutils.file"]={
    get_executable_path_preference=function() return "rawlab" end,
    check_if_bin_exists=function() return "rawlab" end,
    sanitize_filename=function(path) return path end,
  }
  package.loaded["lib/dtutils.system"]={external_command=function()
    local file=assert(io.open(request,"rb"))
    local contents=file:read("*a"); file:close()
    local records={}
    for source, output in contents:gmatch("([0-9a-f]+)\t([0-9a-f]+)\n") do
      outputs[#outputs+1]=unhex(output); write(unhex(output),"DNG")
      records[#records+1]=source .. "\t" .. output .. "\n"
    end
    write(request .. ".results","rawlab-results-v1\n" .. (mode=="partial" and "partial\n" or "complete\n") .. table.concat(records))
    if mode=="throws" then error("launch failed") end
    return mode=="nonzero" and 1 or 0
  end}
  local tmpname=os.tmpname
  os.tmpname=function() return request end
  dofile(script)
  assert(clicked); clicked()
  os.tmpname=tmpname
  assert(exists(request)==(mode~="success"), mode .. ": request retention")
  assert(exists(request .. ".results")==(mode~="success"), mode .. ": receipt retention")
  if mode~="success" then
    for _, message in ipairs(messages) do assert(not message:find("outputs imported"), message) end
  end
  os.remove(request); os.remove(request .. ".results"); os.remove(request .. ".results.cancel")
  for _, output in ipairs(outputs) do os.remove(output) end
end
