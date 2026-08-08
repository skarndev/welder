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
    catch (WelderNativeException ex)
    {
        Check(ex.Message == "boom" && ex.NativeCode == 1,
              "C++ exception -> WelderNativeException");
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
    using (var st = seg.Start)
        Check(st.X == 1, "welded field get (a copy)");
}

Check(Global.Add(2, 3) == 5, "free function");
Check(Global.Greet("cs") == "hi cs", "string param + return");
Global.Answer = 43;
Check(Global.Answer == 43, "namespace variable set/get");
Check(Math.Abs(Global.Golden - 1.618) < 1e-12, "const variable (get-only)");
Check(Inner.Twice(21) == 42, "nested namespace static class");
Check((byte)Level.High == 200, "enum : byte underlying");

if (failures > 0) { Console.Error.WriteLine($"{failures} FAILURES"); return 1; }
Console.WriteLine("ALL PASS");
return 0;
