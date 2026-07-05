---@meta

--- A tiny geometry module for the LuaCATS stub test.
stubdemo = {}

--- Unit constants.
stubdemo.units = {}

--- Cardinal directions.
---@enum stubdemo.Direction
stubdemo.Direction = {
    North = 0,
    East = 1,
    South = 2,
    West = 3,
}

--- Named colors.
---@enum stubdemo.Color
stubdemo.Color = {
    Red = 0,
    Green = 2,
    Blue = 3,
}

--- A shape.
---@class stubdemo.Shape
---@field label string A human-readable label.
stubdemo.Shape = {}

---@return stubdemo.Shape
function stubdemo.Shape.new() end

--- The area of the shape.
---@return number square units
function stubdemo.Shape:area() end

--- A circle.
--- 
--- A round shape with one radius.
---@class stubdemo.Circle : stubdemo.Shape
---@field radius number The radius, in units.
---@operator mul(number): stubdemo.Circle
---@operator eq(stubdemo.Circle): boolean
stubdemo.Circle = {}

---@return stubdemo.Circle
function stubdemo.Circle.new() end

--- Construct from a radius.
---@param radius number the radius
---@return stubdemo.Circle
function stubdemo.Circle.new(radius) end

---@return number
function stubdemo.Circle:area() end

--- Axis-aligned box (aggregate).
---@class stubdemo.Box
---@field width number Width in units.
---@field height number Height in units.
stubdemo.Box = {}

---@return stubdemo.Box
function stubdemo.Box.new() end

---@param width number
---@param height number
---@return stubdemo.Box
function stubdemo.Box.new(width, height) end

--- A polygon.
---@class stubdemo.Polygon
---@field corners stubdemo.Box[] The corner points, as [x, y] pairs.
---@field name string? Optional name.
---@field anchors table<string, stubdemo.Box> Named anchor points.
stubdemo.Polygon = {}

---@return stubdemo.Polygon
function stubdemo.Polygon.new() end

---@param corners stubdemo.Box[]
---@param name string?
---@param anchors table<string, stubdemo.Box>
---@return stubdemo.Polygon
function stubdemo.Polygon.new(corners, name, anchors) end

--- Sum a list of areas.
---@param areas number[] the shapes' areas
---@return number the total area
function stubdemo.total_area(areas) end

--- Radians in a full turn.
---@type number
stubdemo.units.tau = nil

