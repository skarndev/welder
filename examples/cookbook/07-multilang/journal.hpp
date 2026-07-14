#pragma once
// Cookbook 07 — one C++ library, shipped to Python AND Lua.
//
// Everything here is welded for both languages; the per-language shaping happens
// in the annotations (per-language weld_as; mark::only for backend-flavored
// members) and at the entry points (a name style per rod). The same header is
// compiled by the nanobind TU, the sol2 TU and the LuaCATS stub generator.
//
// The backend-flavored methods (save_to) are the one place framework types
// appear: each is gated to its language with mark::only, so the other rod never
// binds — or even inspects — it. Both frameworks' headers are included so the
// class definition is identical in every TU; an inline member that a TU never
// uses is not emitted, so the Python module carries no Lua code and vice versa.
#include <string>
#include <vector>

#include <welder/vocabulary.hpp>

#include <nanobind/nanobind.h> // for the Python-flavored save_to
#include <sol/sol.hpp>         // for the Lua-flavored save_to

namespace
[[=welder::doc("A tiny journalling library.")]]
journal {

// C++ spells its members camelCase; each rod's NAME STYLE reshapes them (PEP 8
// for Python, snake_case for Lua) — no per-language annotations needed for that.
struct
[[
  =welder::weld(welder::lang::py, welder::lang::lua),
  =welder::doc("A dated journal entry.")
]]
Entry {
    std::string title;
    std::string body;

    Entry() = default;
    Entry(std::string t, std::string b) : title{std::move(t)}, body{std::move(b)} {}

    [[=welder::doc("Render the entry as a single line."),
      =welder::returns("`title: body`")]]
    std::string renderLine() const { return title + ": " + body; }
};

struct
[[
  =welder::weld(welder::lang::py, welder::lang::lua),
  =welder::doc("An append-only collection of entries.")
]]
Notebook {
    [[=welder::doc("Append one entry.")]]
    void addEntry(
        [[=welder::doc("the entry to keep")]] const Entry& e) {
        entries_.push_back(e);
    }

    [[=welder::doc("How many entries are stored."),
      =welder::returns("the entry count")]]
    int entryCount() const { return static_cast<int>(entries_.size()); }

    // One entity, a DIFFERENT name per language: weld_as is verbatim and
    // per-language, so Python spells it pythonically and Lua idiomatically.
    [[=welder::weld_as(welder::lang::py, "to_text"),
      =welder::weld_as(welder::lang::lua, "as_string"),
      =welder::doc("Render every entry, one per line.")]]
    std::string renderAll() const {
        std::string out;
        for (const auto& e : entries_) { out += e.renderLine() + "\n"; }
        return out;
    }

    // Backend-flavored implementations of one CONCEPT ("save this somewhere"),
    // each bound only for its language (mark::only) and both surfaced under the
    // SAME public name (weld_as "save_to" — verbatim, so the style can't drift
    // them apart):
    //  - Python: any file-like object, e.g. the result of open() or io.StringIO.
    [[=welder::mark::only(welder::lang::py), =welder::weld_as("save_to"),
      =welder::doc("Write the rendered journal to a file-like object.")]]
    void saveToFileLike(nanobind::object file) const {
        file.attr("write")(nanobind::str(renderAll().c_str()));
    }

    //  - Lua: a writer callback, the idiomatic Lua sink. The sol2 rod converts
    //    sol::protected_function natively, but the LuaCATS stub rod (no sol2
    //    dependency) cannot know that — mark::trust_bindable(lua) vouches for the
    //    signature, so the stub emits it (the param types as `any`).
    [[=welder::mark::only(welder::lang::lua),
      =welder::mark::trust_bindable(welder::lang::lua),
      =welder::weld_as("save_to"),
      =welder::doc("Pass each rendered line to a writer function.")]]
    void saveToWriter(sol::protected_function write) const {
        for (const auto& e : entries_) { write(e.renderLine()); }
    }

  private:
    std::vector<Entry> entries_;
};

[[
  =welder::weld(welder::lang::py, welder::lang::lua),
  =welder::doc("Make an entry with a default body."),
  =welder::returns("the new entry")
]]
Entry makeEntry([[=welder::doc("the entry title")]] std::string title) {
    return Entry{std::move(title), "(empty)"};
}

[[=welder::weld(welder::lang::py, welder::lang::lua),
  =welder::doc("The journal text-format version.")]]
inline constexpr int FORMAT_VERSION{1};

} // namespace journal