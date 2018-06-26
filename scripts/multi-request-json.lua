-- Module instantiation
package.cpath=package.cpath .. ';lib/?.so'

local cjson = require "cjson"
local cjson2 = cjson.new()
local cjson_safe = require "cjson.safe"
package.path=package.path .. ';lib/?.lua'
local mime = require "mime"
-- Initialize the pseudo random number generator
-- Resource: http://lua-users.org/wiki/MathLibraryTutorial
math.randomseed(os.time())
math.random(); math.random(); math.random()

-- Shuffle array
-- Returns a randomly shuffled array
function shuffle(paths)
  local j, k
  local n = #paths

  for i = 1, n do
    j, k = math.random(n), math.random(n)
    paths[j], paths[k] = paths[k], paths[j]
  end

  return paths
end

function decode_json_from_file(file)
  local data = {}
  local content

  -- Check if the file exists
  -- Resource: http://stackoverflow.com/a/4991602/325852
  local f=io.open(file,"r")
  if f~=nil then 
    content = f:read("*all")

    io.close(f)
  else
    -- Return the empty array
    return lines
  end

  -- Translate Lua value to/from JSON
  data = cjson.decode(content)
  return data
end

json_data = decode_json_from_file(c_default_name)

-- Load URL paths from the file
function load_request_objects_from_data()
  if next(json_data) == nil then
    return lines
  end

  return shuffle(json_data["request"])
end

-- Load URL requests from file
requests = load_request_objects_from_data(c_default_name)

-- Check if at least one path was found in the file
if #requests <= 0 then
  print("multiplerequests: No requests found.")
  os.exit()
end

-- print("multiplerequests: Found " .. #requests .. " requests")

url = function()
  return json_data["url"]
end

-- Initialize the requests array iterator
counter = 1

request = function()
  -- Get the next requests array element
  local request_object = requests[counter]

  -- Increment the counter
  counter = counter + 1

  -- If the counter is longer than the requests array length then reset it
  if counter > #requests then
    counter = 1
  end

  local body
  if request_object.bodyType == "base64" then
--      body = mime.b64(request_object.body)
  else
      body = request_object.body
  end
  -- Return the request object with the current URL path
  return wrk.format(request_object.method, request_object.path, request_object.headers, body)
end
