local P = {}
P.__index = P
function P:new(x, y) return setmetatable({X = x, Y = y}, P) end
function P:Len2() return self.X * self.X + self.Y * self.Y end
local acc = 0
for r = 0, 99 do
  local list = {}
  for i = 0, 9999 do list[i + 1] = P:new(i % 100, r) end
  for _, p in ipairs(list) do acc = (acc + p:Len2()) % 1000003 end
end
print(acc)
