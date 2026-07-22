# Binding a type

Every rod goes through the same entry point — `welder::welder<Rod>::weld_type<T>(m)`,
where `Rod` is any of the [shipped rods](../backends/index.md#the-rods-welder-ships)
(`welder::rods::pybind11::rod<>`, `welder::rods::nanobind::rod<>`,
`welder::rods::sol2::rod`, `welder::rods::luabridge::rod`) — which reflects `T` and
emits its whole surface. This
page covers what "whole surface" means for a class: data members, constructors,
methods, and operators. **The annotations and the resolution are identical across
rods;** only the emitted target-language surface differs. Each member obeys the
[resolution rule](annotations.md#the-resolution-rule) — excludes, includes, and the
type's policy decide what participates.

The examples below weld one struct and show how it looks from each language. The
C++ is the same; pick your tab.

!!! example "In the cookbook"

    [Recipe 01 — One of everything](../cookbook/hello.md) welds a type (fields,
    methods, operators, the synthesized aggregate constructor) alongside an enum,
    a free function and a namespace variable; [Recipe 06](../cookbook/templates.md)
    does the same for template instantiations.

## Data members

Public data members bind as read/write attributes. (Protected members can join
them — see
[`policy::weld_protected`](annotations.md#policyweld_protected-expose-the-protected-surface);
private members never bind.)

```cpp
struct [[=welder::weld(welder::lang::py, welder::lang::lua)]]
Point {
    double x{0.0};
    double y{0.0};
};
```

=== ":simple-python: Python"

    ```pycon
    >>> p = Point(); p.x = 3.0; p.y = 4.0
    >>> p.x, p.y
    (3.0, 4.0)
    ```

=== ":simple-lua: Lua"

    ```lua
    local p = Point(); p.x = 3.0; p.y = 4.0
    print(p.x, p.y)   --> 3.0  4.0
    ```

A `const` member binds **read-only** (rebinding the attribute is rejected); an
otherwise-mutable member binds read/write.

### Read-only without `const`: `mark::no_reassign`

Sometimes you want a member the target language can *mutate in place* but not
*reassign wholesale* — classically a mutable STL container: `scene.entities` should
stay appendable, but `scene.entities = [...]` (rebinding the whole attribute) should
be an error. Making the C++ member `const` would forbid the in-place mutation too, so
that is the wrong tool.

`[[=welder::mark::no_reassign]]` forces the read-only *binding* while leaving the C++
member mutable:

```cpp
struct [[=welder::weld(welder::lang::py, welder::lang::lua)]]
Scene {
    [[=welder::mark::no_reassign]] std::vector<Entity> entities;
};
```

The read-only binding still hands out a **live reference**, so in-place mutation
writes straight through to the C++ object; only rebinding the attribute is rejected:

=== ":simple-python: Python"

    ```pycon
    >>> s = Scene()
    >>> s.entities.append(Entity())   # in-place mutation writes through
    >>> s.entities = []               # AttributeError: can't set attribute
    ```

=== ":simple-lua: Lua"

    ```lua
    local s = Scene()
    table.insert -- (via the bound container) mutates in place
    s.entities = {}   --> error (read-only field)
    ```

It is exactly the binding a `const` member gets (pybind11 `def_readonly`, nanobind
`def_ro`, sol2 `sol::readonly`, LuaBridge3 a getter-only `addProperty`, a
`(read-only)` note in the [LuaCATS stub](stubs.md)) — asked for without the `const`.
The mark is scopable per language (`mark::no_reassign(welder::lang::py)` — read-only
in Python, read/write elsewhere) and is a no-op on an already-`const` member. It
belongs on a **data member** only — placing it on a method, a free function, a type,
or a global is a compile error, not a silent no-op. For the container case
specifically, see [Containers](containers.md).

Every bound member's type must pass the [bindability gate](bindability.md) — if the
rod can't convert it to a meaningful value in the target language, you get a
compile error naming the type, never a silent skip.

## Constructors

welder binds:

- the **default constructor**, if present;
- **each public, non-copy/non-move constructor** → `pybind11::init<…>`;
- for a **baseless aggregate**, a *synthesized field constructor* that brace-inits
  it — giving Python `T(f0, f1, …)`;
- the **copy constructor**, given the target language's own copy spelling
  (Python: the `__copy__`/`__deepcopy__` protocol) — see
  [Copy and move constructors](#copy-and-move-constructors).

!!! note "Why aggregates are special"

    Aggregate initialization is positional and all-or-nothing, so the synthesized
    constructor is only offered when **every** field binds.

```cpp
struct [[=welder::weld(welder::lang::py, welder::lang::lua)]]
Rect {                 // an aggregate: no user ctors, no bases
    double w{0.0};
    double h{0.0};
};
```

=== ":simple-python: Python"

    ```pycon
    >>> Rect(2.0, 3.0).w      # synthesized field constructor
    2.0
    ```

=== ":simple-lua: Lua"

    ```lua
    print(Rect(2.0, 3.0).w)   --> 2.0   (synthesized field constructor)
    ```

### NSDMI defaults on the field constructor

The fields after the last one **without** a default member initializer are the
*omissible suffix*: aggregate initialization fills omitted trailing elements
from their NSDMIs, and the synthesized constructor mirrors that. Python attaches
the NSDMI values as real keyword defaults (so a later field can also be set *by
keyword*, skipping earlier defaulted ones); the Lua rods expose one constructor
arity per omissible tail; the [LuaCATS stub](stubs.md) marks the suffix `?`.

```cpp
struct [[=welder::weld(welder::lang::py, welder::lang::lua)]]
Window {
    std::string title;      // required — no NSDMI
    int width{800};         // the omissible suffix...
    int height{600};
    bool resizable{true};
};
```

=== ":simple-python: Python"

    ```pycon
    >>> w = Window("editor")            # suffix filled from the NSDMIs
    >>> w.width, w.height, w.resizable
    (800, 600, True)
    >>> Window("editor", height=900).height   # keyword skips past width
    900
    ```

=== ":simple-lua: Lua"

    ```lua
    local w = Window("editor")          -- the shortest arity
    print(w.width)                      --> 800
    print(Window("editor", 1024).width) --> 1024
    ```

An NSDMI'd field declared *before* a required one stays required — a parameter
list allows no gaps, exactly like C++ default function arguments. Two Python
wrinkles: a default whose type needs registration (a welded class or enum
instance) is attached at runtime but spelled `...` in signatures and `.pyi`
stubs (an object repr is not a valid stub expression), and a move-only field's
value cannot be copied off the probe instance, so such a field is omissible in
Lua's arity form but carries no Python default. **Const members keep a struct an
aggregate**: an immutable settings-style type binds with read-only fields while
the synthesized constructor (and its defaults) still brace-initializes it.

!!! warning "Defaults convert at registration time"

    The Python default values are converted to Python objects **eagerly**, when
    the aggregate registers — so a default whose type is itself welded (that
    `...`-spelled case) must already be *registered* at that point. Within a
    module weld the walk follows **declaration order**, so declare the field's
    type before the first opening of the namespace that carries the aggregate
    (an umbrella header that pre-opens a submodule namespace for a `doc`
    annotation moves that submodule to the front of the walk — open it *after*
    the types its aggregates default to). Getting this wrong raises
    `std::bad_cast` at import.

Compare with a type that declares its own constructors — each public one binds:

```cpp
struct [[=welder::weld(welder::lang::py, welder::lang::lua)]]
Rect {
    double w{0.0};
    double h{0.0};

    Rect() = default;
    Rect(double width, double height) : w{width}, h{height} {}
};
```

### Copy and move constructors

The **copy constructor** does not ride the ordinary per-constructor path — it
gets the target language's own copy spelling instead. The Python rods bind it
as the copy protocol alone — `__copy__` and `__deepcopy__(memo)` — so
`copy.copy(obj)` / `copy.deepcopy(obj)` just work on any copy-constructible
welded type. It is deliberately **not** exposed as a `Rect(other)` init
overload: that spelling is a C++-ism (Python copies through the `copy` module,
not a copy constructor) and would collide with a one-argument constructor of
your own. The C++ payload is duplicated by the copy constructor (the
deep/shallow distinction there is the constructor's own — value members
duplicate, a pointer member copies as a pointer). The Lua rods ignore all of
it: Lua has no copy protocol, exactly as they ignore `doc` and `return_policy`.

```pycon
>>> import copy
>>> a = Rect(2.0, 3.0)
>>> b = copy.copy(a)      # a second C++ object, copy-constructed
>>> b.w = 9.0
>>> a.w
2.0
```

The protocol is **subclass-faithful**: like Python's own copy machinery it
transfers *state*, never calling `__init__` — a `type(self).__new__` shell, the
C++ payload copy-constructed in place, then the instance `__dict__` **and any
`__slots__`-declared attributes** carried over (slot names are collected the
way pickle collects them, so a subclass that keeps its state out of `__dict__`
copies whole; shallow for `copy.copy`, deep-copied through the memo for
`copy.deepcopy`, so shared references dedup and cycles terminate). A Python
subclass therefore copies as itself — its type, attributes and overridden
virtuals intact:

```pycon
>>> class Dotted(Brush):
...     def stroke(self): return "dotted"
>>> d = Dotted()
>>> copy.copy(d).paint()   # C++ still dispatches into the Python override
'paint:dotted'
```

For a type with virtual methods this keeps working because the
`WELDER_PY_TRAMPOLINE(Tramp, Base)` macro also declares a **copy-from-base
constructor** on the trampoline (see
[Overriding virtual methods](inheritance.md)) — the backend needs it to build
the trampoline payload on a subclass shell. A hand-rolled trampoline without
one is a compile error on copyable types (welder names the fix); welder's
*generated* trampolines carry it automatically.

The protocol is **independent of your constructors**: `__copy__`/`__deepcopy__`
copy-construct the C++ payload directly, never through Python's constructor
overload resolution. So even a permissive constructor of yours (one taking a
generic Python object, say) that would happily accept a `T`-instance argument
never interferes — the copy always duplicates faithfully, and your constructor
still serves everything else.

Admission mirrors the default constructor's: an *implicit* copy constructor
rides along whenever the type is copy-constructible (nothing to mark, so
`policy::opt_in`'s default-out does not apply), while a *declared* one's
explicit marks are honored — `[[=welder::mark::exclude]] T(const T&);`
suppresses the copy protocol, per language when the mark is scoped
(`exclude(welder::lang::py)`). A deleted or inaccessible copy constructor
simply means no copy protocol; the type still binds.

The **move constructor** never binds at all — no target language has move
semantics — so it is skipped structurally, and `mark::exclude` on one is a
harmless no-op. Asking for it is diagnosed: an `include`/`only` mark on a move
constructor is a hard compile error naming the copy protocol as what actually
crosses the boundary.

### Parameter names → keyword arguments (Python)

When **every** parameter of a signature is named, welder passes the names through
as `py::arg`, so they work as Python keyword arguments:

```pycon
>>> Rect(width=2.0, height=3.0).area()
6.0
```

Lua has no keyword arguments, so this is a Python-only convenience; the same
constructor is still callable positionally there.

## Methods and static methods

Member functions bind as methods; `static` member functions as static/free
functions on the type. Overloads are all registered on every rod — the Python
rods (pybind11/nanobind) chain them, and the **sol2** rod groups a name's
overloads into one `sol::overload(…)` — so each overload dispatches on its arguments
at call time.

```cpp
struct [[=welder::weld(welder::lang::py, welder::lang::lua)]]
Rect {
    double w{0.0}, h{0.0};

    [[=welder::doc("The area of the rectangle.")]]
    double area() const { return w * h; }

    static Rect square(double s) { return Rect{s, s}; }
};
```

=== ":simple-python: Python"

    ```pycon
    >>> Rect(2.0, 3.0).area()
    6.0
    >>> Rect.square(5.0).area()
    25.0
    ```

=== ":simple-lua: Lua"

    ```lua
    print(Rect(2.0, 3.0):area())     --> 6.0   (method call uses `:`)
    print(Rect.square(5.0):area())   --> 25.0  (static uses `.`)
    ```

!!! tip "Accessor pairs can bind as properties"

    A `get_x()`/`set_x(v)` (or `x()`/`x(v)`) pair doesn't have to bind as two
    methods: mark the functions `[[=welder::getter]]` / `[[=welder::setter]]` and
    welder builds one idiomatic read/write property instead — see
    [Properties](properties.md).

## Overloaded operators

An operator binds under the target language's special method / metamethod, told
apart unary vs. binary by arity. **Member and freestanding operators both
participate**: welder also sweeps a welded type's enclosing namespace for
operators *anchored* on it (an operand is the type) — the ADL surface C++
callers see — and each `(operator, arity)` slot reaches the backend as **one
combined group**, so a member `operator+` and a free `operator+` overload one
another instead of colliding. The mapping differs per language:

=== ":simple-python: Python"

    | C++ | Python | C++ | Python |
    |---|---|---|---|
    | `operator+` | `__add__` | `operator==` | `__eq__` |
    | `operator-` (binary) | `__sub__` | `operator-` (unary) | `__neg__` |
    | `operator*` | `__mul__` | `operator[]` | `__getitem__` |
    | `operator()` | `__call__` | `operator<` | `__lt__` |

    Arithmetic, bitwise, comparison, call and subscript operators are covered.
    See the [Python rods page](../backends/python.md#operators-become-dunders)
    for the full table.

=== ":simple-lua: Lua"

    | C++ | Lua | C++ | Lua |
    |---|---|---|---|
    | `operator+` | `__add` | `operator==` | `__eq` |
    | `operator-` (binary) | `__sub` | `operator-` (unary) | `__unm` |
    | `operator*` | `__mul` | `operator[]` | `__index` |
    | `operator()` | `__call` | `operator<` | `__lt` |

    Lua's metamethod set is smaller and asymmetric — `!=`, `>`, `>=` are *derived*
    from `__eq`, `__lt`, `__le`, so you don't bind them. See the
    [Lua rod page](../backends/lua.md#operators-become-metamethods) for the full
    table.

```cpp
struct [[=welder::weld(welder::lang::py, welder::lang::lua)]]
Vec2 {
    double x{0.0}, y{0.0};
    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-() const { return {-x, -y}; }        // unary → __neg__ / __unm
    bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
};

// Freestanding operators anchored on Vec2 bind too — marks on them resolve
// exactly like marks on members (under Vec2's policy):
Vec2 operator*(const Vec2& v, double k);   // __mul__ / __mul
Vec2 operator*(double k, const Vec2& v);   // Python __rmul__ (2.0 * v); Lua __mul
std::ostream& operator<<(std::ostream& os, const Vec2& v);  // __str__ / __tostring
```

Three free-operator shapes get special treatment:

- **Reflected operands** — a free operator with the welded type on the *right*
  (`operator*(double, Vec2)`) binds under Python's reflected dunder (`__rmul__`,
  or the mirrored comparison), so `2.0 * v` works exactly as in C++. Lua needs
  no distinction: a metamethod receives its operands as written, and the
  overload's exact signature dispatches it.
- **The ostream inserter** — `operator<<(std::ostream&, T)` never binds as a
  shift: it becomes Python `__str__` / Lua `__tostring` (the `std::ostream&`
  parameter is exempt from the bindability gate).
- **Python's `NotImplemented` protocol** — every binary arithmetic / comparison
  dunder is bound as a *true* operator: a failed operand conversion returns
  `NotImplemented` (letting Python try the other operand's reflected method)
  instead of raising `TypeError`.

!!! warning "Hidden friends are invisible to reflection"

    A **hidden friend** operator (defined inline inside the class as a
    `friend`) can be found only by ADL — P2996 reflection enumerates neither a
    class's friends nor its ADL surface, so welder cannot see it. Move it to
    namespace scope, or bind it by hand on the class handle `weld_type` returns.

### `operator<=>` synthesizes the comparisons

The spaceship itself never binds (`std::strong_ordering` has no target-language
counterpart). Instead, a participating `operator<=>` — member or free —
**synthesizes the relational operators** as plain rewritten expressions
(`a < b`, …), so C++'s own rewriting rules pick the overload and the target
language sees exactly what a C++ caller sees:

- **Python** gets `__lt__` / `__le__` / `__gt__` / `__ge__`; a heterogeneous
  `operator<=>(int)` compares both ways (`a < 5`, and `5 < a` via the reflected
  protocol).
- **Lua** gets `__lt` / `__le` only — Lua derives `>`, `>=` and `~=` by
  swapping operands / negating. For a heterogeneous spaceship both operand
  orders are registered (`5 < a` reaches `__lt(5, a)`).
- `==` is **never** synthesized: C++ itself only rewrites `==` from
  `operator==`. A *defaulted* spaceship implicitly declares a defaulted
  `operator==`, and that member binds through the ordinary operator path — so a
  `= default` spaceship yields the full comparison set with one line.
- An **explicit** relational operator beats synthesis for its slot (mirroring
  C++'s preference for non-rewritten candidates), and marks on the spaceship
  scope the synthesis per language like any member mark. Only the *operand*
  types face the bindability gate — the ordering return type never crosses.

```cpp
struct [[=welder::weld(welder::lang::py, welder::lang::lua)]]
Version {
    int maj{0}, mnr{0};
    auto operator<=>(const Version&) const = default;  // <, <=, >, >=, ==, !=
};
```

!!! info "Deliberately not mapped"

    In-place compound assignment (`operator+=`) is **not** mapped — Python falls
    back to `a = a + b` via `__add__`. Nor are `&&`, `||`, `++`, `--`, or
    `operator=` (a special member).

## Nested types

*(The full decision flowcharts for everything on this page live on
[The resolution algorithm](../resolution.md).)*

A class or enum declared **inside** a welded type resolves like any other class
member: the *outer's* policy plus the nested type's own
[`exclude` / `include` / `only` marks](annotations.md#mark-per-member-overrides)
decide participation. A nested type never carries (or needs) a `weld` of its own
— nested types are interface helpers of their enclosing type, and the enclosing
`weld` is the discovery marker. Nesting recurses (`Outer::Inner::Innermost`),
private nested types never bind, and protected ones follow
[`policy::weld_protected`](annotations.md).

```cpp
struct [[=welder::weld(welder::lang::py, welder::lang::lua)]]
Robot {
    struct Sensor { double range{1.5}; };          // binds as Robot.Sensor
    enum class Mode { idle, active };              // binds as Robot.Mode
    struct [[=welder::mark::exclude]] Impl { };    // bound nowhere

    Sensor sensor{};                               // fine: Sensor is registered
    void set_mode(Mode m);                         // fine: Mode is registered
};
```

Because the nested types register with the outer, members whose signatures use
them pass the [bindability gate](bindability.md) with no extra annotation — and a
signature naming a nested type that does *not* participate (excluded, private,
or forward-declared) is a hard compile error, not a runtime surprise.

=== ":simple-python: Python"

    The nested type is registered with the enclosing class as its scope, exactly
    like a hand-written `py::class_<Robot::Sensor>(robot_cls, "Sensor")`:

    ```python
    s = mymod.Robot.Sensor()        # scoped: module.Outer.Inner
    Robot.Sensor.__qualname__       # "Robot.Sensor" — stubs nest too
    mymod.Robot.Mode.active         # a nested IntEnum
    ```

    An *unscoped* nested enum exports its values onto the **class**
    (`Robot.quiet`), mirroring C++'s `Robot::quiet`.

=== ":simple-lua: Lua"

    Both Lua rods expose the same access chain — `mod.Robot.Sensor` (sol2 places
    the usertype on the outer's table; LuaBridge3 moves the class table onto the
    outer as a static entry). The generated
    [LuaCATS stub](stubs.md) declares it under the dotted name
    (`---@class mod.Robot.Sensor`):

    ```lua
    local s = mod.Robot.Sensor.new()
    print(mod.Robot.Mode.active)    -- a nested value table
    print(mod.Robot.quiet)          -- unscoped enum: mirrored onto the class
    ```

### Member type aliases

A **member type alias** can pull an *outside* type into the class's binding —
the class-scope counterpart of
[welding through a namespace alias](templates.md#welding-through-an-alias-the-namespace-sweep),
and the natural home for a vendor type your class's interface uses:

```cpp
struct [[=welder::weld(welder::lang::py, welder::lang::lua)]]
Console {
    using Dial = vendor::Dial;          // unwelded vendor type  → Console.Dial
    using Ints = vendor::Roll<int>;     // a specialization      → Console.Ints
    using Names = std::vector<std::string>; // castable          → skipped
    using Bot  = Robot;                 // already welded        → skipped

    vendor::Dial dial{};                // fine: Console's own aliases are
    vendor::Dial read_dial() const;     // visible to the bindability gate
};
```

The rule: a member alias participates **iff its target fails the bindability
gate** — registering exactly the types that otherwise couldn't cross the
boundary. A target the gate already passes (natively castable, a bindable STL
wrapper, welded, or otherwise registered) converts without help, so registering
it again would be redundant or an outright duplicate — those aliases are
skipped, which is why ordinary `value_type` / `iterator` conventions cost
nothing. The alias's own `exclude` / `include` / `only` marks and the outer's
policy apply as for any member; `weld_as` on the alias renames verbatim. Under
[tack welding](namespaces-modules.md) every complete type passes the greedy
gate, so member aliases never participate there — a tacked third-party header's
alias conventions stay inert by construction.

Combined with `mark::exclude` on a *declared* nested type, an alias is also the
class-scope **rename escape**: the exclude takes the type out of the sweep, and
the alias re-registers it under its own name:

```cpp
struct [[=welder::mark::exclude]] Core { /* … */ };
using Heart = Core;                     // → Console.Heart
```

!!! info "Scope of the gate's alias knowledge"

    An alias is unrecoverable from the type it names, so the gate learns about
    alias registrations only for the **class being bound**: members of `Console`
    may freely use `Console`'s alias-registered types, but *another* class (or a
    free function) naming `vendor::Dial` in a signature still needs a
    [`trust_bindable` hatch](trust-casters.md) — consistent with the
    namespace-alias blind spot. Two classes aliasing the *same* unwelded target
    would each register it (an import-time framework error); two aliases of one
    target inside a single class are diagnosed at compile time.

!!! warning "Alias targets with virtual methods (Python)"

    The
    [trampoline generator](inheritance.md#overriding-virtual-methods-from-python)
    deliberately runs without a bindability oracle, so it never sees member
    aliases — an alias-registered
    target with overridable virtuals needs a **hand-written** trampoline
    (spelled through the alias, e.g. `Outer::Buf`) or a `bind_flat` opt-out; the
    Python rods' trampoline gate will tell you at compile time.

!!! tip "Excluding + welding manually"

    To keep a nested type out of the outer's surface but still bind it (flat,
    under a name of your choosing), combine `mark::exclude` with an explicit
    `weld` and weld it manually — the exclude removes it from the sweep, the
    `weld` keeps the gate satisfied for manual registration:

    ```cpp
    struct [[=welder::weld(welder::lang::py), =welder::mark::exclude]]
    Cursor { /* … */ };  // nested inside Outer
    // …
    weld::weld_type<Outer::Cursor>(m, "OuterCursor");  // flat, renamed
    ```

!!! info "Flattened bases keep their nested types to themselves"

    A nested type registers exactly once, with its **declaring** class. A
    non-welded base's members are flattened into the derived binding, but its
    nested types are not — two derived types flattening one mixin would register
    the same type twice. A flattened signature naming one therefore fails the
    gate until you weld the base (or trust/exclude the member).

## Chaining on the returned handle

`weld_type` returns the rod's own class handle — pybind11's `py::class_<T>`,
nanobind's `nb::class_<T>`, sol2's `sol::usertype<T>` — so hand-written
framework registrations chain right on: welder lays the reflected boilerplate,
you add what it shouldn't guess (a lambda-backed helper, a member you
[excluded](annotations.md#mark-per-member-overrides) to bind manually, a custom
return-value policy):

```cpp
auto cls = weld::weld_type<Rectangle>(m);          // welder binds the reflected surface
cls.def("scaled", [](const Rectangle& r, double k) // …and you weld on by hand
        { return Rectangle{r.width * k, r.height * k}; });
```

`weld_function` likewise returns the bound function object where the framework
has one (the Python rods, sol2), and
[`weld_namespace_as_submodule`](namespaces-modules.md#binding-a-namespace-as-a-submodule) returns the new
submodule handle — every entry point hands back its framework object so
welder-generated and hand-written bindings mix freely.

Next: [Enums](enums.md).
