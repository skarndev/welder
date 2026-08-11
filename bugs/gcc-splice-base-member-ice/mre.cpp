// gcc-16 -std=c++26 -freflection -fsyntax-only mre.cpp
//   => internal compiler error: Segmentation fault
//
// Splicing a member that is declared in a BASE class, on an object whose
// pointer type is itself a splice of a class-TEMPLATE specialization.
// Both ingredients are required; drop either and it compiles:
//   * make E a non-template                                  -> OK
//   * move `m` from A into E                                 -> OK
//   * write `reinterpret_cast<E<2>*>` instead of the splice   -> OK
// static_cast in place of reinterpret_cast ICEs identically.

#include <meta>

struct A { int m{}; };
template <int V> struct E : A {};

template <std::meta::info W, std::meta::info M>
int get(void* p) {
    auto* o{reinterpret_cast<[:W:]*>(p)};
    return (*o).[:M:];
}

int main() { return get<^^E<2>, ^^A::m>(nullptr); }
