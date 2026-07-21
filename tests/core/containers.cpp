// Compile-only lock for the reference-semantic container table & predicates
// (welder/containers.hpp): which STL containers welder binds opaquely, their binder
// kind, and which are contiguous (buffer/ndarray eligible). Pure static_asserts —
// building this target IS the test (tests/CMakeLists.txt: compile.containers).
#include <deque>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <welder/containers.hpp>

namespace {

using welder::container_is_contiguous;
using welder::container_kind;
using welder::container_kind_of;
using welder::is_reference_container;

// --- the supported set: vector (sequence) + map/unordered_map (mapping) -------
static_assert(is_reference_container(^^std::vector<int>));
static_assert(is_reference_container(^^std::vector<std::string>));
static_assert(is_reference_container(^^std::map<std::string, int>));
static_assert(is_reference_container(^^std::unordered_map<int, double>));

static_assert(container_kind_of(^^std::vector<float>) == container_kind::sequence);
static_assert(container_kind_of(^^std::map<int, int>) == container_kind::map);
static_assert(container_kind_of(^^std::unordered_map<int, int>) ==
              container_kind::map);

// --- NOT in scope: deque/list/set/multimap (no framework opaque binder) -------
static_assert(!is_reference_container(^^std::deque<int>));
static_assert(!is_reference_container(^^std::set<int>));
static_assert(!is_reference_container(^^std::multimap<int, int>));
// non-templated / scalar types are never containers
static_assert(!is_reference_container(^^int));
static_assert(!is_reference_container(^^std::string));

// --- contiguity: only vector exposes data() for a zero-copy buffer view -------
static_assert(container_is_contiguous(^^std::vector<double>));
static_assert(!container_is_contiguous(^^std::map<int, int>));
static_assert(!container_is_contiguous(^^std::unordered_map<int, int>));

} // namespace
