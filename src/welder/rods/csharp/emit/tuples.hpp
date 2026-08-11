#pragma once
#include <cstddef>
#include <meta>
#include <string>
#include <utility>

#include <welder/rods/csharp/emit/refs.hpp>
#include <welder/rods/csharp/emit/spellings.hpp>
#include <welder/rods/csharp/type_map.hpp>

/** @file
    The managed half of the **pair/tuple wire**: a `std::pair`/`std::tuple` of
    leaf elements crosses as an array of `welder_opt_wire` slots and surfaces in
    C# as a ValueTuple, so each element needs a read and a write statement
    generated for it.

    Reads and writes are asymmetric on purpose: a write is always an expression
    (it initializes one slot), while a string read needs *statements* — read the
    pointer, marshal it, free it in a `finally`.
*/

namespace welder::inline v0::rods::csharp {

/** One pair/tuple element's WRITE into its wire slot (the C# side). */
template <std::meta::info E>
std::string tuple_slot_write(const std::string& val) {
    constexpr marshal_kind k{classify(E)};
    if constexpr (k == marshal_kind::utf8_string)
        return "new WelderOptWire { Has = 1, S = "
               "NativeMethods.welder_dup_utf8(" + val + ") }";
    else if constexpr (k == marshal_kind::handle)
        return "new WelderOptWire { Has = 1, P = " + val + "._h_" +
               field_ref<bare(E)>() + ".DangerousGetHandle() }";
    else if constexpr (k == marshal_kind::boolean)
        return "new WelderOptWire { Has = 1, I = " + val + " ? 1 : 0 }";
    else if constexpr (type_trait(^^std::is_floating_point_v, bare(E)))
        return "new WelderOptWire { Has = 1, F = (double)" + val + " }";
    else // integral scalar / enum
        return "new WelderOptWire { Has = 1, I = (long)" + val + " }";
}

/** One pair/tuple element's READ out of its wire slot (the C# side); a string
    element needs statements (read + free), the rest are expressions. */
template <std::meta::info E>
std::string tuple_slot_read(const std::string& slot, const std::string& var,
                             const std::string& ind) {
    constexpr marshal_kind k{classify(E)};
    if constexpr (k == marshal_kind::utf8_string)
        return ind + "string " + var + ";\n" + ind + "{\n" + ind +
               "    IntPtr _sp = " + slot + ".S;\n" + ind +
               "    try { " + var +
               " = Marshal.PtrToStringUTF8(_sp) ?? \"\"; }\n" + ind +
               "    finally { NativeMethods.welder_free(_sp); }\n" + ind +
               "}\n";
    else if constexpr (k == marshal_kind::handle)
        return ind + "var " + var + " = new " + type_ref<bare(E)>() + "(" +
               slot + ".P, true);\n";
    else if constexpr (k == marshal_kind::boolean)
        return ind + "var " + var + " = " + slot + ".I != 0;\n";
    else if constexpr (k == marshal_kind::enum_)
        return ind + "var " + var + " = (" + type_ref<bare(E)>() + ")" + slot +
               ".I;\n";
    else if constexpr (type_trait(^^std::is_floating_point_v, bare(E))) {
        constexpr const char* c{scalar_spell(E).cs};
        return ind + "var " + var + " = unchecked((" + std::string{c} + ")" +
               slot + ".F);\n";
    } else {
        constexpr const char* c{scalar_spell(E).cs};
        return ind + "var " + var + " = unchecked((" + std::string{c} + ")" +
               slot + ".I);\n";
    }
}

template <std::meta::info Type, std::size_t... J>
std::string tuple_write_stmts(const std::string& tw,
                                    const std::string& name,
                                    std::index_sequence<J...>) {
    std::string out{"            var " + tw + " = stackalloc WelderOptWire[" +
                    std::to_string(sizeof...(J)) + "];\n"};
    ((out += "            " + tw + "[" + std::to_string(J) + "] = " +
             tuple_slot_write<tuple_element_type(bare(Type), J)>(
                 name + ".Item" + std::to_string(J + 1)) +
             ";\n"),
     ...);
    return out;
}

template <std::meta::info Type, std::size_t... J>
std::string tuple_read_stmts(const std::string& ind,
                                   std::index_sequence<J...>) {
    std::string out{ind + "var _slots = (WelderOptWire*)_r.Data;\n"};
    ((out += tuple_slot_read<tuple_element_type(bare(Type), J)>(
          "_slots[" + std::to_string(J) + "]", "_t" + std::to_string(J), ind)),
     ...);
    out += ind + "NativeMethods.welder_free(_r.Data);\n";
    out += ind + "return (";
    std::size_t i{0};
    ((out += (i++ ? std::string{", "} : std::string{}) + "_t" +
             std::to_string(J)),
     ...);
    out += ");\n";
    return out;
}
} // namespace welder::inline v0::rods::csharp
