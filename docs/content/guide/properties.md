# Properties (getter/setter marks)

C++ encapsulates state behind accessor functions; the target languages spell the
same idea as a **property** — attribute syntax backed by functions. Mark the
accessors and welder builds the idiomatic pair: one function
(`[[=welder::getter]]`) makes a read-only property, two (`+ [[=welder::setter]]`)
make it read/write. The marked functions stop being methods — they *are* the
property.

```cpp
struct [[=welder::weld(welder::lang::py, welder::lang::lua)]]
Circle {
    Circle() = default;

    [[=welder::getter]] [[=welder::doc("The circle's radius.")]]
    double radius() const { return radius_; }

    [[=welder::setter]]
    void radius(double r) { radius_ = r; }

    [[=welder::getter]]
    double get_area() const { return 3.141592653589793 * radius_ * radius_; }

  private:
    double radius_{1.0};
};
```

=== ":simple-python: Python"

    ```pycon
    >>> c = Circle()
    >>> c.radius          # the getter, as attribute read
    1.0
    >>> c.radius = 2.0    # the setter, as attribute write
    >>> c.area            # lone getter -> read-only, prefix stripped
    12.566370614359172
    >>> c.area = 5.0
    AttributeError: property 'area' of 'Circle' object has no setter
    >>> Circle.radius.__doc__
    "The circle's radius."
    ```

=== ":simple-lua: Lua"

    ```lua
    local c = Circle()
    print(c.radius)       --> 1.0
    c.radius = 2.0
    print(c.area)         --> 12.566…
    c.area = 5.0          --> error (read-only)
    ```

A getter must be a **const** member function taking no parameters and returning a
value; a setter takes exactly **one** parameter. Anything else is a designed
compile error, as is a setter whose property has no getter (welder binds no
write-only properties).

## Naming: how accessors become one property

The property's name comes from the **getter** (the setter's spelling never
surfaces):

1. An **explicit name** in the mark wins, verbatim — `getter("radius")` is the
   property's `weld_as` (an actual `weld_as` on an accessor is diagnosed; the
   mark's name *is* the rename tool). Scope and repeat it per language exactly
   like `weld_as`: `getter(welder::lang::py, "radius")`.
2. Otherwise the name **derives** from the identifier: a leading `get`/`set`
   *word* is stripped — `get_area`, `getArea`, `GetArea` and `GET_AREA` all
   derive `area` — and the remainder keeps the identifier's own spelling
   convention (`getMaxSpeed` → `maxSpeed`). Under a
   [name style](naming.md) the identifier is styled first, then stripped, so
   `pep8` turns `getMaxSpeed` into `max_speed`. An `is_` prefix is deliberately
   *not* stripped — `is_ready` already is the idiomatic property name.

**Pairing** matches getter and setter on the *case-normalized word sequence* of
their derived (or explicit) names, so every accessor convention pairs — the
overload style (`radius()` / `radius(double)`), the prefix styles
(`get_x`/`set_x`, `getX`/`setX`, `GetX`/`SetX`), even a mixed-convention pair
(`get_scale` + `SetScale`). Two getters (or two setters) landing on one property
name, or a property name colliding with a bound data member or method, are
compile errors.

## Resolution: properties obey the same marks

Whether an accessor *participates* is the ordinary
[resolution rule](annotations.md#the-resolution-rule) — the type's policy plus
the member's own marks — with two property-specific twists:

- **The accessor mark implies the opt-in.** Under `policy::opt_in` a marked
  getter/setter needs no separate `mark::include` (like `mark::only`, the mark
  is an unambiguous statement of intent). An explicit `exclude` still beats it.
- **Marks are language-scoped.** `getter(welder::lang::py)` makes a property in
  Python while the function stays an ordinary *method* in Lua (the mark doesn't
  cover it). Excluding just the setter for one language
  (`[[=welder::setter, =welder::mark::exclude(welder::lang::lua)]]`) makes the
  property read-only there and read/write everywhere else.

Accessors flatten from non-welded bases like any member, and protected accessors
bind under
[`policy::weld_protected`](annotations.md#policyweld_protected-expose-the-protected-surface).
A **fluent** setter (`Labeled& set_label(std::string)`) works everywhere: the
property protocol has no slot for a setter's return, so every rod discards it —
and its type never faces the [bindability gate](bindability.md). The getter's
return type is the property's value and is gated as usual.

## Per-rod surface

| Rod | read/write | read-only |
|---|---|---|
| pybind11 | `def_property` | `def_property_readonly` |
| nanobind | `def_prop_rw` | `def_prop_ro` |
| sol2 | `sol::property(get, set)` | `sol::readonly_property(get)` |
| LuaBridge3 | `addProperty(get, set)` | `addProperty(get)` |
| LuaCATS stub | `---@field name Type` | same, plus a `(read-only)` note |

The getter's `[[=welder::doc]]` becomes the property's `__doc__` on the Python
rods (and the `---@field` description in the [LuaCATS stub](stubs.md)); a
[`return_policy`](return-policies.md) on the getter is honored there too —
unannotated getters default to the frameworks' property semantics
(`reference_internal` for reference/pointer returns, a copy/move for values).

## Limits (diagnosed, not silent)

- **Static** accessors don't bind as static properties yet — a marked static
  member function is a compile error (bind it as a static method, or expose a
  namespace variable).
- **Virtual** accessors are rejected: both Python rods dispatch overrides
  through a by-name attribute lookup, and a property under that name would
  silently break the override protocol. Wrap the virtual in non-virtual
  accessors instead.
- The marks apply to **member functions only** — on a namespace-scope function
  they are diagnosed (module-level attribute syntax is the
  [namespace variable](namespaces-modules.md) story).