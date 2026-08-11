#pragma once
#include <cctype>
#include <cstddef>
#include <meta>
#include <string>

#include <welder/rods/csharp/document.hpp>
#include <welder/rods/csharp/emit/params.hpp>
#include <welder/rods/csharp/emit/refs.hpp>
#include <welder/rods/csharp/emit/returns.hpp>
#include <welder/rods/csharp/emit/spellings.hpp>
#include <welder/rods/csharp/type_map.hpp>

/** @file
    The per-class **`shared_ptr` box**: a `SafeHandle` whose release frees the
    boxed `std::shared_ptr` copy that pins a shared return's object.

    A `shared_ptr` return crosses as (object pointer, boxed copy). The wrapper
    holds a non-owning view of the object and stores the box as its owner, so the
    C++ object stays alive for exactly as long as the managed view does — and the
    reference count drops when that view is collected or disposed.
*/

namespace welder::inline v0::rods::csharp {

/** The per-class shared_ptr BOX: a SafeHandle whose release frees the
    boxed `shared_ptr` copy pinning a shared return's object. */
template <std::meta::info T>
void ensure_shared(document& doc) {
    static constexpr const char* key{std::define_static_string(
        std::string{"sp:"} + qualified_cpp_name(T))};
    if (!doc.claim_container(key))
        return;
    const std::string sym{std::string{"welder_sp_"} + symtok_v<T> +
                          "_free"};
    // Deferred like the container elements: a shared_ptr payload can be
    // an alias-welded specialization, which has no qualified name to spell
    // here. @see anchor_ref
    const std::string cpp{anchor_ref<T>()};
    doc.record_symbol(sym);
    doc.shim += "void " + sym + "(void* box) { wcs::shim::sp_free<^^" +
                cpp + ">(box); }\n\n";
    doc.pinvoke += "        [LibraryImport(Lib)] internal static partial "
                   "void " + sym + "(IntPtr box);\n";
    doc.containers +=
        "    internal sealed class " + field_ref<T>() +
        "SharedBox : SafeHandle\n    {\n"
        "        internal " + field_ref<T>() +
        "SharedBox(IntPtr handle, bool owns) : base(IntPtr.Zero, owns)\n"
        "        {\n            SetHandle(handle);\n        }\n"
        "        public override bool IsInvalid => handle == "
        "IntPtr.Zero;\n"
        "        protected override bool ReleaseHandle()\n        {\n"
        "            NativeMethods." + sym + "(handle);\n"
        "            return true;\n        }\n    }\n\n";
}
} // namespace welder::inline v0::rods::csharp
