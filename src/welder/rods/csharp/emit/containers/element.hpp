#pragma once
#include <meta>

#include <welder/rods/csharp/document.hpp>

/** @file
    The one declaration the container generators share: how to make sure a
    container's ELEMENT has a wrapper of its own.

    It has to be a forward declaration, because the answer depends on the whole
    family — a nested sequence's element may be a scalar sequence, a welded
    vector, a fixed array — and each of those generators would otherwise have to
    include the others. The definition lives in
    `<welder/rods/csharp/emit/containers.hpp>`, next to the dispatch it belongs
    to.
*/

namespace welder::inline v0::rods::csharp {

/** Generate the wrapper the ELEMENT of a container needs, when that element is
    itself a container (a jagged `vector<vector<T>>`, a `vector<array<T, N>>`).

    A welded element already has its own binding; a container element does not,
    so the outer wrapper's live element views would have nothing to be views
    *of*. Ordering is not a concern — every generator claims its key first, so
    the recursion terminates and each wrapper is emitted exactly once.
    @tparam E a reflection of the (bare) element type.
    @param doc the growing document. */
template <std::meta::info E>
void ensure_element_wrapper(document& doc);

} // namespace welder::inline v0::rods::csharp
