# Trust & type casters

The [bindability gate](bindability.md) is conservative on purpose: it rejects any
program-defined type it can't see welded, because a registration made *outside*
welder (a hand-written pybind11 `class_`, a nanobind `nb::class_`, a sol2 usertype, a
third-party library's bindings) is invisible to a compile-time caster read. There are
three ways to satisfy it.

The first two — the `trust_bindable` marks — are **backend-agnostic vocabulary**;
they suppress the gate wherever the type appears and leave the registration to you.
The third is a **rod-native caster**, so its exact form depends on the rod.

!!! example "In the cookbook"

    [Recipe 07](../cookbook/multilang.md) uses the member mark (a framework type
    the LuaCATS stub rod can't see); [Recipe 08](../cookbook/tack-welding.md) uses
    the type-level form to tack-weld a third-party type appearing in signatures.

## 1. `mark::trust_bindable` — trust one member

A member mark vouches for **that member's type** (or, on a method, the whole
signature). welder skips the gate there; you then own the registration.

```cpp
struct Handmade { int n{0}; };          // welder never sees this welded

struct [[=welder::weld(welder::lang::py)]]
TrustsMember {
    [[=welder::mark::trust_bindable]]   // trust this member's type
    Handmade item;

    int count{0};

    [[=welder::mark::trust_bindable]]   // also trusts a *method's* whole signature
    Handmade make(int n) const { return Handmade{n}; }
};
```

You must register `Handmade` with pybind11 by hand **before** binding the struct
that uses it.

## 2. `trust_bindable<T> = true` — trust a type everywhere

The type-level form is a specializable `bool` variable template (not an attribute).
It trusts `T` **everywhere** it appears — member, parameter, return, *and* container
element, because it folds into `bindable()` itself:

```cpp
struct Handmade2 { int n{0}; };

template <> constexpr bool welder::trust_bindable<Handmade2> = true;

struct [[=welder::weld(welder::lang::py)]]
TrustsType {
    Handmade2 item;                     // trusted → bound
    std::vector<Handmade2> many;        // also cleared: recursion hits a trusted leaf
    int count{0};
};
```

!!! tip "Which trust to reach for"

    Use the **member mark** for a one-off. Use the **type-level** point when a type
    appears in many places and is always registered elsewhere — it also clears
    `T*`, `const T&`, and `std::vector<T>` in one stroke.

## 3. A self-contained rod caster — no trust needed

If you give `T` a **self-contained** caster in the rod's framework — one that
does *not* derive from the rod's registration-needing base — it displaces the
fallback caster. Now `has_native_caster<T>` reports true, the gate passes
**automatically**, and the caster even names the type in generated stubs. No weld, no
trust. The mechanism is standard for each framework:

=== "pybind11"

    A `type_caster<T>` built with `PYBIND11_TYPE_CASTER` (does not derive from
    `type_caster_base`), so `_needs_registration<T>` is *false* and `const_name`
    stubs it cleanly (e.g. as `float`):

    ```cpp
    struct Celsius { double t; };

    namespace pybind11::detail {
    template <> struct type_caster<Celsius> {
        PYBIND11_TYPE_CASTER(Celsius, const_name("float"));
        bool load(handle src, bool) { value.t = src.cast<double>(); return true; }
        static handle cast(const Celsius& c, return_value_policy, handle) {
            return PyFloat_FromDouble(c.t);
        }
    };
    }  // namespace pybind11::detail
    ```

=== "nanobind"

    The same idea with `NB_TYPE_CASTER` in `nanobind::detail` — a caster that isn't
    a base caster flips `is_base_caster_v<make_caster<T>>` to false, so the gate
    clears `T` without a registered class.

=== "Lua (sol2)"

    Lua's leaf question is different: the gate clears `T` when
    `sol::lua_type_of<T>` is a native Lua type rather than `userdata`. A type that
    sol2 already knows how to push/get natively (or one you teach it via a
    `sol_lua_push`/`sol_lua_get` customization) passes without a usertype.

!!! warning "The caster must be visible before the bind"

    Standard framework behavior, not welder-specific: the caster has to be in scope
    **before** welder binds any type using `T`. (gcc-16 happens to defer
    instantiation to end-of-TU so a later caster in the *same* TU also works — but
    relying on that is ill-formed-NDR. Keep the caster ahead of the bind.)

    A caster that *does* derive from the registration-needing base (pybind11's
    `type_caster_base`, nanobind's base caster) still needs its class registered —
    only a self-contained one flips the type to native.

## Summary

| Mechanism | Backend-agnostic? | Scope | Who registers `T` |
|---|---|---|---|
| `mark::trust_bindable` | ✅ vocabulary | one member / one signature | you (by hand, before the bind) |
| `trust_bindable<T> = true` | ✅ vocabulary | `T` everywhere (incl. `T*`, `T&`, `vector<T>`) | you (by hand, before the bind) |
| self-contained rod caster | ⚙️ per rod | `T` everywhere | the caster *is* the conversion |

A future `bindable_as<T>` mapping T to a stub type name is still TODO.

Next: [Generating C++ docs](cpp-docs.md).
