#pragma once
#include <cctype>
#include <cstddef>
#include <meta>
#include <string>

#include <welder/rods/csharp/document.hpp>
#include <welder/rods/csharp/emit/containers/fixed.hpp>
#include <welder/rods/csharp/emit/containers/map.hpp>
#include <welder/rods/csharp/emit/containers/scalar_seq.hpp>
#include <welder/rods/csharp/emit/containers/shared.hpp>
#include <welder/rods/csharp/emit/containers/vector.hpp>
#include <welder/rods/csharp/emit/params.hpp>
#include <welder/rods/csharp/emit/refs.hpp>
#include <welder/rods/csharp/emit/returns.hpp>
#include <welder/rods/csharp/emit/spellings.hpp>
#include <welder/rods/csharp/type_map.hpp>

/** @file
    The **generated container wrappers**: welder's opaque-container model,
    written out as C# classes over the thunks in
    `<welder/rods/csharp/shim/containers.hpp>`.

    A container-typed member or signature does not merely need a *spelling* — it
    needs a whole reference-semantic wrapper class to exist somewhere in the
    emitted `Bindings.cs`, plus its native operations and their P/Invoke
    declarations. These `ensure_*` functions generate exactly one such wrapper
    per distinct instantiation, keyed by the container's display string
    (@ref welder::rods::csharp::document::claim_container), and register the
    wrapper's name so every reference placeholder resolves to it.

    | Family | Wrapper | Element access |
    |---|---|---|
    | `std::vector<Welded>` | `Vector<Element>` | a LIVE view of the element |
    | `std::array<Welded, N>` | `Array<Element>x<N>` | the same, fixed size |
    | `std::vector`/`std::array` of scalars or enums, as a FIELD | `Vector<Scalar>` / `Array<Scalar>x<N>` | a zero-copy `Span<T>` over the C++ buffer |
    | `std::map` / `std::unordered_map` | `Map…` / `UMap…` | a live view for a welded mapped type |
    | `std::shared_ptr<Welded>` | a `SharedBox` SafeHandle | (pins a shared return) |

    Each generator lives in its own file under
    `<welder/rods/csharp/emit/containers/…>`; this header holds the two dispatch
    points and includes them all.

    @ref welder::rods::csharp::collect_containers is the sweep every emission
    hook calls before writing a signature, so the wrapper a parameter or return
    needs always exists by the time it is referenced.
*/

namespace welder::inline v0::rods::csharp {



/** Generate whatever per-type scaffolding @a Type's marshalling needs:
    a vector/array/map wrapper, or a shared_ptr box class. */
template <std::meta::info Type>
void ensure_for(document& doc) {
    constexpr marshal_kind k{classify(Type)};
    if constexpr (k == marshal_kind::seq_ref) {
        if constexpr (is_fixed_sequence(bare(Type)))
            ensure_fixed<bare(Type)>(doc);
        else
            ensure_vector<bare(Type)>(doc);
    } else if constexpr (k == marshal_kind::map_ref) {
        ensure_map<bare(Type)>(doc);
    } else if constexpr (k == marshal_kind::shared_ptr_) {
        ensure_shared<bare(
            std::meta::template_arguments_of(bare(Type))[0])>(doc);
    }
}




/** Collect the generated scaffolding a callable's signature uses. */
template <std::meta::info Fn>
void collect_containers(document& doc) {
    if constexpr (!std::meta::is_constructor(Fn))
        ensure_for<std::meta::return_type_of(Fn)>(doc);
    template for ([[maybe_unused]] constexpr auto p :
                  std::define_static_array(std::meta::parameters_of(Fn))) {
        ensure_for<std::meta::type_of(p)>(doc);
    }
}
} // namespace welder::inline v0::rods::csharp
