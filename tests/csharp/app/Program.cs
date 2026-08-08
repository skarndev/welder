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

// --- inheritance (dedicated Dog + the shared inheritance cases) ----------------

using (var dog = new Dog())
{
    Check(dog.Bark() == "woof" && dog.Kind() == "animal",
          "inherited base method via C# base class");
    dog.Age = 7;
    Check(Global.AgeOf(dog) == 7, "derived instance passes as base param");
    using (var lg = dog.AsLegged())
        Check(lg.Legs == 4, "extra base surface via As-view");
    Check(dog is Animal, "C# base clause (is Animal)");
}

using (var d = new inheritance.Derived())
{
    Check(d.BaseMethod() == 1 && d.DerivedMethod() == 2,
          "shared: welded base chain");
    Check(d is inheritance.Base, "shared: issubclass equivalent");
}
using (var leaf = new inheritance.Leaf())
    Check(leaf.LeafField == 6 && leaf.MidField == 5 && leaf.BaseField == 1,
          "shared: multi-level chain fields");
using (var wm = new inheritance.WithMixin())
    Check(wm.OwnField == 4 && wm.MixinMethod() == 3,
          "shared: non-welded mixin flattened");
using (var th = new inheritance.Through())
    Check(th.ThroughField == 12 && th.WeldedMethod() == 10 &&
              th is inheritance.Welded,
          "shared: welded base through a non-welded bridge");
using (var bot = new inheritance.Bottom())
{
    Check(bot is inheritance.Left && bot is inheritance.Apex,
          "shared: diamond primary chain");
    using (var r = bot.AsRight())
        Check(r is inheritance.Right, "shared: diamond extra base As-view");
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

// --- value-marshalled containers ------------------------------------------------

using (var bk = new Basket())
{
    Check(bk.Nums.Length == 3 && bk.Nums[2] == 3, "vector<int> field -> int[] copy");
    bk.Nums = new[] { 5, 6 };
    Check(bk.Total(new[] { 1 }) == 12, "vector<int> param + field set");
    Check(bk.Find(6) == 1 && bk.Find(9) == null, "optional<int> return");
    Check(bk.Label == null, "optional<string> field (empty)");
    bk.Label = "tag";
    Check(bk.Label == "tag", "optional<string> field set/get");
    var t = bk.Triple();
    Check(t.Length == 3 && Math.Abs(t[1] - 2.5) < 1e-12, "array<double,3> return");
    bk.SetTriple(new[] { 1.0, 2.0, 3.0 });
    Check(Math.Abs(bk.TripSum() - 6.0) < 1e-12, "array<double,3> param");
    try { bk.SetTriple(new[] { 1.0 }); Check(false, "wrong array length must throw"); }
    catch (ArgumentException)
        { Check(true, "array extent mismatch -> ArgumentException"); }
}
using (var mp = Global.MaybePoint(true)!)
    Check(mp.X == 3, "optional<welded> some (owned copy)");
Check(Global.MaybePoint(false) == null, "optional<welded> none -> null");
Check(Global.MaybeLevel(true) == Level.High && Global.MaybeLevel(false) == null,
      "optional<enum>");

// --- reference-semantic vector of welded elements -------------------------------

using (var rt = new Route())
{
    var stops = rt.Stops;                    // a live wrapper over the member
    using (var p1 = new Point(1, 2)) stops.Add(p1);
    using (var p2 = new Point(3, 4)) stops.Add(p2);
    Check(rt.StopCount() == 2, "vector<welded> field wrapper: Add writes through");
    stops[0].X = 10;                          // a live element view
    Check(rt.TotalX(stops) == 13, "live element write-through + wrapper param");
    using (var rev = rt.Reversed())
        Check(rev.Count == 2 && rev[0].X == 3, "owned vector return + indexer");
    stops.Clear();
    Check(rt.StopCount() == 0, "wrapper Clear writes through");
    using (var mine = new VectorPoint())
    {
        using (var p = new Point(7, 0)) mine.Add(p);
        Check(rt.TotalX(mine) == 7, "C#-constructed vector as a param");
        try { _ = mine[5]; Check(false, "OOB index must throw"); }
        catch (ArgumentOutOfRangeException)
            { Check(true, "wrapper indexer bounds-checks (at -> OOR)"); }
    }
}

// --- directors: C# subclasses overriding C++ virtuals --------------------------

using (var sh = new Shape())
{
    Check(sh.Name() == "shape", "director-constructed base behaves as base");
    Check(sh.Describe() == "shape:0", "C++ caller through unoverridden director");
}
using (var sq = new SquareCs())
{
    Check(sq.Name() == "square", "C# override called directly");
    Check(sq.Describe() == "square:4", "C++ virtual dispatch reaches C# overrides");
    Check(Global.DescribeShape(sq) == "square", "dispatch through a base-typed param");
}
using (var bc = new BaseCallerCs())
    Check(bc.Describe() == "mega-shape:0", "base.Name() inside an override terminates");
using (var th = new ThrowerCs())
{
    try { th.Describe(); Check(false, "managed exception should cross"); }
    catch (WelderNativeException ex)
        { Check(ex.NativeCode == 7 && ex.Message == "cs-boom",
                "managed exception round-trips as code 7"); }
}

// --- the shared overridable cases (tests/common/cpp/overridable.hpp) -----------

using (var an = new overridable.Animal())
{
    Check(an.Describe() == "... on 4 legs", "shared: unoverridden virtuals");
    Check(an.Kingdom() == "Animalia", "shared: bind_flat stays a plain method");
}
using (var ld = new LoudDogCs())
    Check(ld.Describe() == "WOOF on 2 legs",
          "shared: C++ caller dispatches into C# overrides");
using (var bird = new CsBird())
    Check(bird.Describe() == "tweet on 2 legs" && bird.Fly() == "soar",
          "shared: inherited + own virtual slots on a derived director");
using (var cy = new CyborgCs())
{
    Check(cy.March(2) == "STEP STEP ", "shared: parameterful virtual override");
    Check(cy.Transmit() == "INT:7|STR:hi", "shared: overloaded virtuals per slot");
    Check(cy.Recharge(5) == 500, "shared: noexcept virtual override");
    Check(cy.Handshake() == "proto=asimov",
          "shared: unbound NVI hook falls through to base");
}

if (failures > 0) { Console.Error.WriteLine($"{failures} FAILURES"); return 1; }
Console.WriteLine("ALL PASS");
return 0;

class SquareCs : Shape
{
    public override string Name() => "square";
    public override int Sides() => 4;
}

class BaseCallerCs : Shape
{
    public override string Name() => "mega-" + base.Name();
}

class ThrowerCs : Shape
{
    public override int Sides() => throw new InvalidOperationException("cs-boom");
}

class LoudDogCs : overridable.Animal
{
    public override string Speak() => "WOOF";
    public override int Legs() => 2;
}

class CsBird : overridable.Bird
{
    public override string Speak() => "tweet";
    public override int Legs() => 2;
    public override string Fly() => "soar";
}

class CyborgCs : overridable.Robot
{
    public override string Obey(string order, int times)
    {
        string r = "";
        for (int i = 0; i < times; i++) r += order.ToUpperInvariant();
        return r;
    }
    public override string Send(int code) => "INT:" + code;
    public override string Send(string text) => "STR:" + text;
    public override int Recharge(int amount) => amount * 100;
}
