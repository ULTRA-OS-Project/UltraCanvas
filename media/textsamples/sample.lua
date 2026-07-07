-- sample.lua — Lua syntax-highlighting sample for the UltraCanvas demo.
local Vector = {}
Vector.__index = Vector

function Vector.new(x, y)
    return setmetatable({ x = x or 0, y = y or 0 }, Vector)
end

function Vector:lengthSquared()
    return self.x * self.x + self.y * self.y
end

function Vector.__add(a, b)
    return Vector.new(a.x + b.x, a.y + b.y)
end

local points = {
    Vector.new(1, 2),
    Vector.new(3, 4),
    Vector.new(-5, 0.5),
}

table.sort(points, function(a, b)
    return a:lengthSquared() < b:lengthSquared()
end)

for i, p in ipairs(points) do
    print(string.format("%d: (%.1f, %.1f)", i, p.x, p.y))
end

local sum = points[1] + points[2]
print("sum = (" .. sum.x .. ", " .. sum.y .. ")")
