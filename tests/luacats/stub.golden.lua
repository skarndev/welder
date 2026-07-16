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
---@class stubdemo.Figure
---@field label string A human-readable label.
---@field dims integer Number of spatial dimensions. (read-only)
stubdemo.Figure = {}

---@return stubdemo.Figure
function stubdemo.Figure.new() end

--- The area of the shape.
---@return number square units
function stubdemo.Figure:area() end

--- A circle.
--- 
--- A round shape with one radius.
---@class stubdemo.Circle : stubdemo.Figure
---@field radius number The radius, in units.
---@operator mul(number): stubdemo.Circle
stubdemo.Circle = {}

--- Construct from a radius.
---@param radius number the radius
---@return stubdemo.Circle
---@overload fun(): stubdemo.Circle
function stubdemo.Circle.new(radius) end

---@return number
function stubdemo.Circle:area() end

--- A scaled copy.
---@param k number uniform factor
---@return stubdemo.Circle
---@overload fun(kx: number, ky: number): stubdemo.Circle
function stubdemo.Circle:scaled(k) end

--- A bit mask (exercises the bitwise metamethods).
---@class stubdemo.Mask
---@field bits integer The raw bits.
---@operator band(stubdemo.Mask): stubdemo.Mask
---@operator bor(stubdemo.Mask): stubdemo.Mask
---@operator bxor(stubdemo.Mask): stubdemo.Mask
---@operator bnot: stubdemo.Mask
---@operator shl(integer): stubdemo.Mask
---@operator shr(integer): stubdemo.Mask
stubdemo.Mask = {}

---@return stubdemo.Mask
---@overload fun(bits: integer): stubdemo.Mask
function stubdemo.Mask.new() end

--- Axis-aligned box (aggregate).
---@class stubdemo.Rect
---@field width number Width in units.
---@field height number Height in units.
stubdemo.Rect = {}

---@return stubdemo.Rect
---@overload fun(width: number, height: number): stubdemo.Rect
function stubdemo.Rect.new() end

--- A polygon.
---@class stubdemo.Polygon
---@field corners stubdemo.Rect[] The corner points, as [x, y] pairs.
---@field name string? Optional name.
---@field anchors table<string, stubdemo.Rect> Named anchor points.
stubdemo.Polygon = {}

---@return stubdemo.Polygon
---@overload fun(corners: stubdemo.Rect[], name: string?, anchors: table<string, stubdemo.Rect>): stubdemo.Polygon
function stubdemo.Polygon.new() end

--- A measuring gauge.
---@class stubdemo.Gauge
---@field needle stubdemo.Gauge.Needle The current needle.
stubdemo.Gauge = {}

---@return stubdemo.Gauge
---@overload fun(needle: stubdemo.Gauge.Needle): stubdemo.Gauge
function stubdemo.Gauge.new() end

--- Select the range.
---@param r stubdemo.Gauge.Range the new range
function stubdemo.Gauge:set_range(r) end

--- The needle position.
---@class stubdemo.Gauge.Needle
---@field angle number Angle in degrees.
stubdemo.Gauge.Needle = {}

---@return stubdemo.Gauge.Needle
---@overload fun(angle: number): stubdemo.Gauge.Needle
function stubdemo.Gauge.Needle.new() end

--- Operating range.
---@enum stubdemo.Gauge.Range
stubdemo.Gauge.Range = {
    Low = 0,
    High = 1,
}

--- Sum a list of areas.
---@param areas number[] the shapes' areas
---@return number the total area
function stubdemo.total_area(areas) end

--- Describe an area, optionally with a unit.
---@param area number the area
---@return string
---@overload fun(area: number, unit: string): string
function stubdemo.describe(area) end

--- Radians in a full turn.
---@type number
stubdemo.units.tau = nil

--- A homogeneous pair.
---@class stubdemo.Pair
---@field first number First of the pair.
---@field second number Second of the pair.
stubdemo.Pair = {}

---@return stubdemo.Pair
---@overload fun(first: number, second: number): stubdemo.Pair
function stubdemo.Pair.new() end

