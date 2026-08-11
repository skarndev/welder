#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <meta>
#include <tuple>
#include <utility>

#include <welder/bind_traits.hpp>              // param_types / aggregate_fields / stringify
#include <welder/rods/csharp/shim/convert.hpp> // to_cpp / guarded
#include <welder/rods/csharp/type_map.hpp>

/** @file
    **One thunk body per bound entity kind**: the templates the generated
    `extern "C"` functions delegate into.

    Every generated thunk is a one-liner — its whole body is a call to one of
    these, parameterized by the exact member reflection re-derived through the
    shared lookup layer (`named_member` / `ctor_at` / `named_field`). That is the
    trampolines rod's splice-don't-respell idiom applied to the C ABI: only the
    wire types (`void*`, `std::int32_t`, `const char*`, `welder_error*`, …) and
    the entity anchor spellings (`^^ns::Type`, `"name"`) appear as text in the
    generated file, so a drifted header fails the shim build instead of calling
    the wrong overload.

    All of them route their result through @ref welder::rods::csharp::shim::guarded
    or @ref welder::rods::csharp::shim::caught, so the error contract holds
    uniformly: a C++ exception lands in the trailing `welder_error*` out-param
    and never unwinds through the C ABI.
*/

namespace welder::inline v0::rods::csharp::shim {

/** Invoke the exact callable @a Fn (spliced — never overload resolution over
    wire types) with no wire arguments. @a lead is the object reference for a
    nonstatic member, or nothing.
    @tparam Fn   a reflection of the callable.
    @tparam Lead the leading object argument's type, if any (deduced).
    @param lead the object to invoke on, for a nonstatic member.
    @return the call's result. */
template <std::meta::info Fn, class... Lead>
constexpr decltype(auto) invoke_exact(Lead&&... lead) {
    return std::invoke(&[:Fn:], std::forward<Lead>(lead)...);
}

/** @ref invoke_exact with wire arguments converted per @a Fn's declared
    parameter types.
    @tparam Fn   a reflection of the callable.
    @tparam Tup  the forwarded wire-argument tuple's type (deduced).
    @tparam J    the parameter indices.
    @tparam Lead the leading object argument's type, if any (deduced).
    @param wire the wire arguments.
    @param lead the object to invoke on, for a nonstatic member.
    @return the call's result. */
template <std::meta::info Fn, class Tup, std::size_t... J, class... Lead>
constexpr decltype(auto) invoke_wired(Tup&& wire, std::index_sequence<J...>,
                                      Lead&&... lead) {
    static constexpr auto ps{::welder::detail::param_types<Fn>()};
    return std::invoke(&[:Fn:], std::forward<Lead>(lead)...,
                       to_cpp<ps[J]>(std::get<J>(std::forward<Tup>(wire)))...);
}

/** An instance method thunk body: @a W is the WELDED class the handle points to
    (which may differ from @a Fn's declaring class when a non-welded base's
    member was flattened in — the pointer-to-member invocation converts).
    @tparam W    a reflection of the welded class the handle points at.
    @tparam Fn   a reflection of the method.
    @tparam Wire the wire parameter types (deduced).
    @param self the object handle.
    @param err  the error slot.
    @param w    the wire arguments.
    @return the wire form of the method's result. */
template <std::meta::info W, std::meta::info Fn, class... Wire>
auto method(void* self, welder_error* err, Wire... w) noexcept {
    using Obj = [:W:];
    auto* obj{static_cast<Obj*>(self)};
    return guarded<std::meta::return_type_of(Fn),
                   ::welder::return_policy_of(Fn, lang::cs)>(
        err, [&]() -> decltype(auto) {
        if constexpr (sizeof...(Wire) == 0)
            return invoke_exact<Fn>(*obj);
        else
            return invoke_wired<Fn>(std::forward_as_tuple(w...),
                                    std::index_sequence_for<Wire...>{}, *obj);
    });
}

/** A static-method / free-function thunk body.
    @tparam Fn   a reflection of the callable.
    @tparam Wire the wire parameter types (deduced).
    @param err the error slot.
    @param w   the wire arguments.
    @return the wire form of the call's result. */
template <std::meta::info Fn, class... Wire>
auto function(welder_error* err, Wire... w) noexcept {
    return guarded<std::meta::return_type_of(Fn),
                   ::welder::return_policy_of(Fn, lang::cs)>(
        err, [&]() -> decltype(auto) {
        if constexpr (sizeof...(Wire) == 0)
            return invoke_exact<Fn>();
        else
            return invoke_wired<Fn>(std::forward_as_tuple(w...),
                                    std::index_sequence_for<Wire...>{});
    });
}

/** Heap-construct @a W through the exact constructor @a Ctor's parameter list.
    @tparam W    a reflection of the class to construct.
    @tparam Ctor a reflection of the constructor whose parameters type the call.
    @tparam Tup  the forwarded wire-argument tuple's type (deduced).
    @tparam J    the parameter indices.
    @param wire the wire arguments.
    @return the new object. */
template <std::meta::info W, std::meta::info Ctor, class Tup, std::size_t... J>
void* construct_wired(Tup&& wire, std::index_sequence<J...>) {
    using Obj = [:W:];
    static constexpr auto ps{::welder::detail::param_types<Ctor>()};
    return new Obj(to_cpp<ps[J]>(std::get<J>(std::forward<Tup>(wire)))...);
}

/** A declared-constructor thunk body: heap-construct via the exact constructor
    @a Ctor's parameter list (arguments arrive exactly typed, so overload
    resolution selects @a Ctor itself).
    @tparam W    a reflection of the class.
    @tparam Ctor a reflection of the constructor.
    @tparam Wire the wire parameter types (deduced).
    @param err the error slot.
    @param w   the wire arguments.
    @return the new object's handle (null on failure). */
template <std::meta::info W, std::meta::info Ctor, class... Wire>
void* construct(welder_error* err, Wire... w) noexcept {
    return caught<void*>(err, [&]() -> void* {
        if constexpr (sizeof...(Wire) == 0)
            return new [:W:]();
        else
            return construct_wired<W, Ctor>(std::forward_as_tuple(w...),
                                            std::index_sequence_for<Wire...>{});
    });
}

/** @ref construct's director twin: heap-construct the DIRECTOR type @a Dir
    (which inherits @a W's constructors) and hand back the @a W-adjusted
    pointer — the handle convention is "points at the welded type".
    @tparam Dir  a reflection of the generated director subclass.
    @tparam W    a reflection of the welded class.
    @tparam Ctor a reflection of the constructor.
    @tparam Wire the wire parameter types (deduced).
    @param err the error slot.
    @param w   the wire arguments.
    @return the new object's handle, adjusted to @a W. */
template <std::meta::info Dir, std::meta::info W, std::meta::info Ctor,
          class... Wire>
void* construct_as(welder_error* err, Wire... w) noexcept {
    return caught<void*>(err, [&]() -> void* {
        if constexpr (sizeof...(Wire) == 0)
            return static_cast<[:W:]*>(new [:Dir:]());
        else
            return static_cast<[:W:]*>(
                static_cast<[:Dir:]*>(construct_wired<Dir, Ctor>(
                    std::forward_as_tuple(w...),
                    std::index_sequence_for<Wire...>{})));
    });
}

/** @ref default_construct's director twin.
    @tparam Dir a reflection of the generated director subclass.
    @tparam W   a reflection of the welded class.
    @param err the error slot.
    @return the new object's handle, adjusted to @a W. */
template <std::meta::info Dir, std::meta::info W>
void* default_construct_as(welder_error* err) noexcept {
    return caught<void*>(
        err, [&]() -> void* { return static_cast<[:W:]*>(new [:Dir:]()); });
}

/** The default-constructor thunk body (the synthesized form has no reflection
    to name).
    @tparam W a reflection of the class.
    @param err the error slot.
    @return the new object's handle. */
template <std::meta::info W>
void* default_construct(welder_error* err) noexcept {
    return caught<void*>(err, [&]() -> void* { return new [:W:](); });
}

/** Heap-construct aggregate @a W field-for-field (C++20 parenthesized aggregate
    initialization).
    @tparam W   a reflection of the aggregate.
    @tparam Tup the forwarded wire-argument tuple's type (deduced).
    @tparam J   the field indices.
    @param wire the wire arguments.
    @return the new object. */
template <std::meta::info W, class Tup, std::size_t... J>
void* aggregate_wired(Tup&& wire, std::index_sequence<J...>) {
    using Obj = [:W:];
    static constexpr auto fs{::welder::detail::aggregate_fields<Obj>()};
    return new Obj(
        to_cpp<std::meta::type_of(fs[J])>(std::get<J>(std::forward<Tup>(wire)))...);
}

/** The synthesized aggregate field-constructor thunk body.
    @tparam W    a reflection of the aggregate.
    @tparam Wire the wire parameter types (deduced).
    @param err the error slot.
    @param w   the wire arguments (one per field).
    @return the new object's handle. */
template <std::meta::info W, class... Wire>
void* aggregate_construct(welder_error* err, Wire... w) noexcept {
    return caught<void*>(err, [&]() -> void* {
        if constexpr (sizeof...(Wire) == 0)
            return new [:W:]();
        else
            return aggregate_wired<W>(std::forward_as_tuple(w...),
                                      std::index_sequence_for<Wire...>{});
    });
}

/** The copy thunk body backing the managed `Clone()` (the admitted copy
    constructor's spelling — C# has no copy-constructor protocol).
    @tparam W a reflection of the class.
    @param self the object to copy.
    @param err  the error slot.
    @return the copy's handle. */
template <std::meta::info W>
void* clone(void* self, welder_error* err) noexcept {
    using Obj = [:W:];
    return caught<void*>(err, [&]() -> void* {
        return new Obj(*static_cast<const Obj*>(self));
    });
}

/** The destroy thunk body (the managed `SafeHandle`'s release). Never throws —
    a throwing destructor would terminate, exactly as it should.
    @tparam W a reflection of the class.
    @param self the object to delete. */
template <std::meta::info W>
void destroy(void* self) noexcept {
    delete static_cast<[:W:]*>(self);
}

/** A data-member getter thunk body (@a W as in @ref method). A **non-const
    class-typed member** hands out a live, non-owning view aliasing the member
    (the runtime rods' `def_readwrite` reference_internal semantics — the C#
    side ties the view to the parent); a const class member and every other
    kind cross by value as before.
    @tparam W   a reflection of the welded class the handle points at.
    @tparam Mem a reflection of the data member.
    @param self the object handle.
    @param err  the error slot.
    @return the wire form of the member's value, or its address for a live view. */
template <std::meta::info W, std::meta::info Mem>
auto field_get(void* self, welder_error* err) noexcept {
    using Obj = [:W:];
    // Reach the member through its DECLARING class, not through W. For a member
    // W declares itself these are the same type and static_cast is a no-op; for
    // one FLATTENED IN from a base it is the explicit base adjustment the splice
    // would otherwise have to infer -- and inferring it is what gcc-16 ICEs on
    // (segfault at the `(*obj).[:Mem:]` below, for W = a class-template
    // specialization whose base arrives through a std::conditional_t alias).
    using Owner = [:std::meta::parent_of(Mem):];
    auto* obj{static_cast<Owner*>(static_cast<Obj*>(self))};
    if constexpr (is_handle_like(classify(std::meta::type_of(Mem))) &&
                  !std::meta::is_const_type(std::meta::type_of(Mem))) {
        return caught<void*>(err, [&]() -> void* {
            // Direct splice access (not &[:Mem:]): gcc-16 rejects the
            // ADDRESS-OF form for protected data (and, separately, the
            // spliced address nested in a larger expression), while plain
            // member access works for public and protected alike — the core
            // field_access idiom. Two statements for the second gotcha.
            auto& r{(*obj).[:Mem:]};
            return static_cast<void*>(&r);
        });
    } else {
        return guarded<std::meta::type_of(Mem)>(
            err, [&]() -> decltype(auto) { return (*obj).[:Mem:]; });
    }
}

/** A data-member setter thunk body (copy-assigns through the wire value).
    @tparam W    a reflection of the welded class the handle points at.
    @tparam Mem  a reflection of the data member.
    @tparam Wire the wire value's type (deduced).
    @param self the object handle.
    @param err  the error slot.
    @param w    the wire value. */
template <std::meta::info W, std::meta::info Mem, class Wire>
void field_set(void* self, welder_error* err, Wire w) noexcept {
    using Obj = [:W:];
    auto* obj{static_cast<Obj*>(self)};
    guarded<^^void>(err, [&] {
        (*obj).[:Mem:] =
            to_cpp<std::meta::remove_cv(std::meta::type_of(Mem))>(w);
    });
}

/** A container-typed field's address — the live wrapper view's target.
    @tparam C   a reflection of the enclosing welded class.
    @tparam Mem a reflection of the container-typed member.
    @param self the object handle.
    @param err  the error slot.
    @return the member's address. */
template <std::meta::info C, std::meta::info Mem>
void* field_addr(void* self, welder_error* err) noexcept {
    using Cls = [:C:];
    return caught<void*>(err, [&]() -> void* {
        auto& r{static_cast<Cls*>(self)->[:Mem:]};
        return static_cast<void*>(std::addressof(r));
    });
}

/** Whole-container assignment INTO a field from another wrapped instance (the
    property's set half: `obj.Nums = ...` copies contents, C++ `operator=`).
    @tparam C   a reflection of the enclosing welded class.
    @tparam Mem a reflection of the container-typed member.
    @param self the object handle.
    @param src  a handle to the source container.
    @param err  the error slot. */
template <std::meta::info C, std::meta::info Mem>
void field_assign(void* self, void* src, welder_error* err) noexcept {
    using Cls = [:C:];
    caught<int>(err, [&]() -> int {
        using MT = std::remove_cvref_t<decltype(static_cast<Cls*>(self)
                                                    ->[:Mem:])>;
        static_cast<Cls*>(self)->[:Mem:] =
            *static_cast<MT*>(src);
        return 0;
    });
}

/** The spaceship-comparison thunk body: evaluate `*obj <=> rhs` through
    C++'s own operator-rewriting rules (so member/free/reversed spaceship
    overloads all resolve exactly as for a C++ caller — the pybind rods'
    synthesized_comparison idiom) and collapse the ordering to a wire int:
    `-1` less, `0` equivalent, `1` greater, `2` unordered (partial_ordering).
    The C# side derives the four relational operators from it.
    @tparam W    a reflection of the welded class.
    @tparam P    a reflection of the declared operand type.
    @tparam Wire the operand's wire type (deduced).
    @param self the left operand's handle.
    @param err  the error slot.
    @param w    the right operand, on the wire.
    @return the collapsed ordering. */
template <std::meta::info W, std::meta::info P, class Wire>
std::int32_t compare(void* self, welder_error* err, Wire w) noexcept {
    using Obj = [:W:];
    auto* obj{static_cast<Obj*>(self)};
    return caught<std::int32_t>(err, [&]() -> std::int32_t {
        const auto c{*obj <=> to_cpp<P>(w)};
        if (c < 0)
            return -1;
        if (c > 0)
            return 1;
        if (c == 0)
            return 0;
        return 2;
    });
}

/** The stringifier thunk body: run the swept free ostream inserter through
    @ref welder::detail::stringify and dup the text (the managed `ToString()`
    frees it via `welder_free`).
    @tparam W  a reflection of the welded class.
    @tparam Fn a reflection of the inserter.
    @param self the object handle.
    @param err  the error slot.
    @return the malloc'd UTF-8 text. */
template <std::meta::info W, std::meta::info Fn>
const char* stringify_text(void* self, welder_error* err) noexcept {
    using Obj = [:W:];
    auto* obj{static_cast<Obj*>(self)};
    return caught<const char*>(err, [&]() -> const char* {
        return dup(::welder::detail::stringify<Obj, Fn>(*obj));
    });
}

/** The upcast thunk body: adjust a @a From handle to its @a To base subobject
    — a compiled `static_cast`, so multiple/virtual-inheritance offsets are the
    ABI's own. Pure pointer math: no error slot.
    @tparam From a reflection of the derived class.
    @tparam To   a reflection of the base class.
    @param self the derived handle (may be null).
    @return the base-adjusted handle. */
template <std::meta::info From, std::meta::info To>
void* upcast(void* self) noexcept {
    if (!self)
        return nullptr;
    return static_cast<[:To:]*>(static_cast<[:From:]*>(self));
}

/** A namespace-variable getter thunk body.
    @tparam Var a reflection of the variable.
    @param err the error slot.
    @return the wire form of its value. */
template <std::meta::info Var>
auto var_get(welder_error* err) noexcept {
    return guarded<std::meta::type_of(Var)>(
        err, [&]() -> decltype(auto) { return [:Var:]; });
}

/** A namespace-variable setter thunk body.
    @tparam Var  a reflection of the variable.
    @tparam Wire the wire value's type (deduced).
    @param err the error slot.
    @param w   the wire value. */
template <std::meta::info Var, class Wire>
void var_set(welder_error* err, Wire w) noexcept {
    guarded<^^void>(err, [&] {
        [:Var:] = to_cpp<std::meta::remove_cv(std::meta::type_of(Var))>(w);
    });
}

} // namespace welder::inline v0::rods::csharp::shim
