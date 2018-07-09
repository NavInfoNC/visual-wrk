-- Module instantiation
package.cpath=package.cpath .. ';/usr/local/lib/visual_wrk/?.so'
package.path=package.path .. ';/usr/local/lib/visual_wrk/?.lua'

local cjson = require "cjson"
local cjson2 = cjson.new()
local cjson_safe = require "cjson.safe"
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

function shuffle_labels_by_weight(label_table, weight_table) 
  local offset = 0
  local labels = {}

  local n = #label_table
  for i = 1, n do 
    local label = label_table[i]
    local weight = weight_table[label]
    for j = 1, weight do
      labels[j + offset] = label
    end
    offset = offset + weight
  end

  return shuffle(labels)
end

function decode_json_from_file(file)
  local data = {}
  local content

  if file == nil then
    return lines;
  end

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

g_json_data = decode_json_from_file(c_json_file)

-- Load URL paths from the file
function load_request_objects_from_data()
  if g_json_data == nil or next(g_json_data) == nil then
    return lines
  end

  local mixed_requests = {}
  local mixed_counter = {}
  local mixed_labels = {}

  if g_json_data["mixed_test"] ~=nil then
    local mixed_test_json = g_json_data["mixed_test"]
    local mixed_file_num = #mixed_test_json

    local mixed_label = {}
    local mixed_weight = {}
    for i = 1, mixed_file_num do
      local single_test_json = mixed_test_json[i]
      mixed_label[i] = single_test_json["label"]
      json_tmp = decode_json_from_file(single_test_json["file"])
      if json_tmp == nil then
        os.exit()
      end

      local requests = shuffle(json_tmp["request"])
      if requests == nill then
        os.exit()
      end

      mixed_requests[mixed_label[i]] = requests
      mixed_counter[mixed_label[i]] = 1;
      mixed_weight[mixed_label[i]] = single_test_json["weight"]
    end
    mixed_labels = shuffle_labels_by_weight(mixed_label, mixed_weight)
  else
    if next(g_json_data["request"]) == nil then
      return lines
    end
    mixed_requests["default"] = shuffle(g_json_data["request"])
    mixed_counter["default"] = 1;
  end

  return mixed_requests, mixed_counter, mixed_labels, g_json_data["url"]
end

-- Load URL requests from file
g_mixed_requests, g_mixed_counter, g_mixed_labels, g_url = load_request_objects_from_data()

g_mixed_label_counter = 1

-- Initialize the requests array iterator
request = function()
  local label = g_mixed_labels[g_mixed_label_counter]

  if label ~= nil then
    g_mixed_label_counter = g_mixed_label_counter + 1
    if g_mixed_label_counter > #g_mixed_labels then
      g_mixed_label_counter = 1
    end
  else
    label = "default"
  end

  -- Get the next requests array element
  local request_object = g_mixed_requests[label][g_mixed_counter[label]]

  -- Increment the counter
  g_mixed_counter[label] = g_mixed_counter[label] + 1

  -- If the counter is longer than the requests array length then reset it
  if g_mixed_counter[label] > #g_mixed_requests[label] then
    g_mixed_counter[label] = 1
  end

  local body
  if request_object.bodyType == "base64" then
      body = mime.unb64(request_object.body)
  else
      body = request_object.body
  end
  -- Return the request object with the current URL path
  return wrk.format(request_object.method, request_object.path, request_object.headers, body)
end
