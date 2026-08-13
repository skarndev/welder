#pragma once
#include <string>
#include <utility>

#include <welder/rods/csharp/document/artifacts.hpp>
#include <welder/rods/csharp/text.hpp> // emit_doc_comment

/** @file
    The **enum handle**: a C# `enum : <underlying>` accumulated by the driver's
    per-enumerator hook and flushed by RAII, on the same rationale as
    @ref welder::rods::csharp::class_writer.

    An enum needs no native thunks at all — it crosses as its underlying value —
    so this handle only ever writes managed text. Unlike Python, C# *has* a
    per-member documentation slot, so an enumerator's `[[=welder::doc]]` becomes
    its own `<summary>` rather than folding into the type's.
*/

namespace welder::inline v0::rods::csharp {

/** An enum handle: a C# `enum : <underlying>` accumulated by `add_enumerator`,
    flushed by RAII (same rationale as @ref class_writer). No native thunks — an
    enum crosses as its underlying value. */
struct enum_writer {
    document* doc{nullptr};     /**< The shared document (null = moved-from). */
    std::string* sink{nullptr}; /**< An outer class's members for a nested enum. */
    std::string cs_name{};      /**< The C# enum name (the leaf). */
    std::string cs_ns{};        /**< The enclosing C# namespace (`""` = root). */
    std::string doc_text{};     /**< The enum summary doc. */
    std::string underlying{};   /**< The C# underlying-type spelling (`int`, …). */
    std::string values{};       /**< Accumulated `        Name = value,` lines. */

    enum_writer() = default;
    enum_writer(const enum_writer&) = delete;
    enum_writer& operator=(const enum_writer&) = delete;
    enum_writer(enum_writer&& o) noexcept { *this = std::move(o); }
    enum_writer& operator=(enum_writer&& o) noexcept {
        doc = o.doc;
        sink = o.sink;
        cs_name = std::move(o.cs_name);
        cs_ns = std::move(o.cs_ns);
        doc_text = std::move(o.doc_text);
        underlying = std::move(o.underlying);
        values = std::move(o.values);
        o.doc = nullptr;
        return *this;
    }
    ~enum_writer() {
        if (!doc)
            return;
        std::string& out{sink ? *sink : doc->section(cs_ns).types};
        emit_doc_comment(out, "    ", doc_text.empty() ? nullptr : doc_text.c_str());
        out += "    public enum " + cs_name + " : " + underlying + "\n    {\n";
        out += values;
        out += "    }\n\n";
    }
};

} // namespace welder::inline v0::rods::csharp
