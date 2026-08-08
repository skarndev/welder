// The managed round-trip over the generated bindings (tests/csharp/cpp/cases.hpp
// -> shim.cpp + Bindings.cs -> this app). Asserts behavior across the C ABI:
// construction/Dispose, field + method-backed properties, overloads, string and
// enum crossing, welded handles, the error contract, statics and namespace
// variables. Exit code is the test result (CTest csharp.roundtrip).
using System;
using csharp_cases;

int failures = 0;

void Check(bool ok, string what)
{
    if (!ok) { Console.Error.WriteLine($"FAIL: {what}"); failures++; }
    else Console.WriteLine($"ok: {what}");
}

using (var p = new Point(3, 4))
{
    Check(p.X == 3 && p.Y == 4, "param ctor + field properties");
    p.X = 10;
    Check(p.X == 10, "field setter");
    Check(p.Stamp == 7, "no_reassign field readable");
    Check(p.Sum() == 14, "scalar method");
    p.Offset(1, 1);
    Check(p.X == 11 && p.Y == 5, "overload (2-arg)");
    p.Offset(1);
    Check(p.X == 12 && p.Y == 6, "overload (1-arg)");
    Check(p.Label() == "(12,6)", "string return");
    Check(p.Hue() == Color.Red, "enum return");
    p.Depth = 9;
    Check(p.Depth == 9, "method-backed property");
    using (var t = p.Translated(1, 1))
        Check(t.X == 13 && t.Y == 7 && p.X == 12, "welded return is a copy");
    using (var c = p.Clone())
    {
        c.Offset(1);
        Check(c.X == 13 && p.X == 12, "Clone is a copy");
    }
    try
    {
        p.Explode();
        Check(false, "exception should have crossed");
    }
    catch (ArgumentOutOfRangeException ex)
    {
        Check(ex.Message == "boom", "std::out_of_range -> ArgumentOutOfRangeException");
    }
}

using (var d = new Point())
    Check(d.X == 0 && d.Y == 0, "default ctor");

using (var o = Point.Origin())
    Check(o.X == 0, "static method returning welded");

using (var s = new Size(20, 10))
    Check(s.Width == 20 && s.Height == 10, "aggregate field ctor");

using (var a = new Point(1, 2))
using (var b = new Point(5, 6))
using (var seg = new Segment(a, b))
{
    Check(seg.Span() == 8, "welded by-value params");
    Check(!seg.Degenerate(), "bool return");
    seg.Start.X = 100; // a live view: the write goes through to the C++ member
    Check(seg.Start.X == 100 && seg.Span() == -91, "class field is a live view");
}

Check(Global.Add(2, 3) == 5, "free function");
Check(Global.Greet("cs") == "hi cs", "string param + return");
Global.Answer = 43;
Check(Global.Answer == 43, "namespace variable set/get");
Check(Math.Abs(Global.Golden - 1.618) < 1e-12, "const variable (get-only)");
Check(Inner.Twice(21) == 42, "nested namespace static class");
Check((byte)Level.High == 200, "enum : byte underlying");

// --- ownership / return policies ---------------------------------------------

var holder = new Holder();
var view = holder.Item();          // reference_internal -> live view, pins holder
view.X = 55;
Check(holder.ItemX() == 55, "reference_internal view writes through");
using (var snap = holder.ItemCopy())
{
    snap.X = 77;
    Check(holder.ItemX() == 55, "copy policy snapshots");
}
Check(holder.Peek(false) == null, "null pointer return -> C# null");
using (var peeked = holder.Peek(true)!)
    Check(peeked.X == 55, "reference pointer return is a view");
holder = null!;                    // drop the only direct reference...
GC.Collect();
GC.WaitForPendingFinalizers();
GC.Collect();
Check(view.X == 55, "view's __owner pins the parent across GC");
using (var made = Global.MakePoint(6, 7)!)
    Check(made.X == 6, "factory pointer return adopted (owned)");

// --- the exception taxonomy ----------------------------------------------------

try { Global.Reject(-1); Check(false, "reject(-1) should throw"); }
catch (ArgumentException ex) { Check(ex.Message == "negative", "invalid_argument -> ArgumentException"); }
try { Global.Reject(1); Check(false, "reject(1) should throw"); }
catch (ArithmeticException ex) { Check(ex.Message == "too big", "overflow_error -> ArithmeticException"); }

// --- operators (dedicated Point + the shared operators cases) ------------------

using (var pa = new Point(1, 2))
using (var pb = new Point(3, 4))
{
    using (var ps = pa + pb)
        Check(ps.X == 4 && ps.Y == 6, "member operator +");
    Check(pa == new Point(1, 2), "operator == compares values");
    Check(pa != pb, "synthesized operator !=");
    Check(pa != null && !(pa == null), "null protocol on ==/!=");
    Check(pa.Equals(new Point(1, 2)), "Equals via ==");
}

using (var va = new operators.Vec(1, 2))
using (var vb = new operators.Vec(3, 4))
{
    using (var vs = va + vb)
        Check(vs.X == 4 && vs.Y == 6, "shared: Vec +");
    using (var vn = -va)
        Check(vn.X == -1 && vn.Y == -2, "shared: unary -");
    using (var vm = va * 2.0)
        Check(vm.X == 2, "shared: Vec * double");
    Check(va[0] == 1 && va[1] == 2, "shared: indexer from operator[]");
    Check(va != vb && va == new operators.Vec(1, 2), "shared: Vec ==/!=");
}
using (var mt = new operators.Meters(1) + new operators.Feet(10))
    Check(Math.Abs(mt.Value - 4.048) < 1e-9, "shared: heterogeneous welded +");
using (var cn = new operators.Coin(5) + new operators.Coin(7))
    Check(cn.Cents == 12, "shared: free anchored +");
using (var mm = new operators.Mixed(1) + new operators.Mixed(2))
    Check(mm.V == 3, "shared: member + free in one slot (member)");
using (var mi = new operators.Mixed(1) + 41)
    Check(mi.V == 42, "shared: member + free in one slot (free, int rhs)");
using (var sc = new operators.Scaled(2.5))
{
    Check(sc.ToString() == "Scaled(2.5)", "shared: ostream << -> ToString()");
    using (var sr = 2.0 * sc)
        Check(Math.Abs(sr.F - 5.0) < 1e-12, "shared: reflected operand *");
}
Check(new operators.Version(1, 2) < new operators.Version(1, 3), "shared: defaulted <=> synthesizes <");
Check(new operators.Version(1, 2) == new operators.Version(1, 2), "shared: defaulted <=> implicit ==");
Check(new operators.Temp(1) < new operators.Temp(2) && new operators.Temp(2) >= new operators.Temp(2),
      "shared: custom <=> relationals");
using (var acct = new operators.Account(3))
{
    Check(acct < 5 && acct == 3 && acct != 4, "shared: heterogeneous <=> + ==");
    Check(2 < acct && 3 <= acct, "shared: reversed heterogeneous <=>");
}

// --- the shared retpolicy cases (tests/common/cpp/retpolicy.hpp) ---------------

using (var owner = new retpolicy.Owner())
{
    var v = owner.View();          // reference_internal (shared case)
    v.V = 9;
    Check(owner.InnerV() == 9, "shared retpolicy: view aliases");
    using (var s2 = owner.Snapshot())
    {
        s2.V = 1;
        Check(owner.InnerV() == 9, "shared retpolicy: snapshot copies");
    }
}

if (failures > 0) { Console.Error.WriteLine($"{failures} FAILURES"); return 1; }
Console.WriteLine("ALL PASS");
return 0;
