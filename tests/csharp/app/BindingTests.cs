// The managed round-trip over the generated bindings (tests/csharp/cpp/cases.hpp
// -> shim.cpp + Bindings.cs -> these xUnit facts, run via `dotnet test`).
// Asserts behavior across the C ABI: construction/Dispose, field + method-backed
// properties, overloads, string and enum crossing, welded handles, the error
// contract, ownership/views, operators, inheritance, directors and containers.
// Everything lives in ONE class: xUnit runs facts of a single class
// sequentially, which the shared native library wants.
using System;
using Xunit;
using csharp_cases;

public class BindingTests
{
    [Fact]
    public void ClassSurface()
    {
        using var p = new Point(3, 4);
        Assert.True(p.X == 3 && p.Y == 4, "param ctor + field properties");
        p.X = 10;
        Assert.Equal(10, p.X);
        Assert.Equal(7, p.Stamp); // no_reassign field readable
        Assert.Equal(14, p.Sum());
        p.Offset(1, 1); // overload (2-arg)
        Assert.True(p.X == 11 && p.Y == 5, "overload (2-arg)");
        p.Offset(1); // overload (1-arg)
        Assert.True(p.X == 12 && p.Y == 6, "overload (1-arg)");
        Assert.Equal("(12,6)", p.Label()); // string return
        Assert.Equal(Color.Red, p.Hue());  // enum return
        p.Depth = 9; // method-backed property
        Assert.Equal(9, p.Depth);
        using (var t = p.Translated(1, 1))
            Assert.True(t.X == 13 && t.Y == 7 && p.X == 12,
                        "welded return is a copy");
        using (var c = p.Clone())
        {
            c.Offset(1);
            Assert.True(c.X == 13 && p.X == 12, "Clone is a copy");
        }
        var ex = Assert.Throws<ArgumentOutOfRangeException>(() => p.Explode());
        Assert.Equal("boom", ex.Message); // std::out_of_range mapping
    }

    [Fact]
    public void ConstructionForms()
    {
        using (var d = new Point())
            Assert.True(d.X == 0 && d.Y == 0, "default ctor");
        using (var o = Point.Origin())
            Assert.Equal(0, o.X); // static method returning welded
        using (var s = new Size(20, 10))
            Assert.True(s.Width == 20 && s.Height == 10, "aggregate field ctor");
    }

    [Fact]
    public void ClassFieldsAreLiveViews()
    {
        using var a = new Point(1, 2);
        using var b = new Point(5, 6);
        using var seg = new Segment(a, b);
        Assert.Equal(8, seg.Span()); // welded by-value params
        Assert.False(seg.Degenerate());
        seg.Start.X = 100; // a live view: the write goes through to C++
        Assert.True(seg.Start.X == 100 && seg.Span() == -91,
                    "class field is a live view");
    }

    [Fact]
    public void NamespaceSurface()
    {
        Assert.Equal(5, Global.Add(2, 3));
        Assert.Equal("hi cs", Global.Greet("cs"));
        Global.Answer = 43;
        Assert.Equal(43, Global.Answer);
        Assert.True(Math.Abs(Global.Golden - 1.618) < 1e-12,
                    "const variable (get-only)");
        Assert.Equal(42, Inner.Twice(21)); // nested namespace static class
        Assert.Equal(200, (byte)Level.High); // enum : byte underlying
    }

    [Fact]
    public void OwnershipAndReturnPolicies()
    {
        var holder = new Holder();
        var view = holder.Item(); // reference_internal -> live view, pins holder
        view.X = 55;
        Assert.Equal(55, holder.ItemX()); // view writes through
        using (var snap = holder.ItemCopy())
        {
            snap.X = 77;
            Assert.Equal(55, holder.ItemX()); // copy policy snapshots
        }
        Assert.Null(holder.Peek(false)); // null pointer return -> C# null
        using (var peeked = holder.Peek(true)!)
            Assert.Equal(55, peeked.X); // reference pointer return is a view
        holder = null!; // drop the only direct reference...
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
        Assert.Equal(55, view.X); // view's __owner pins the parent across GC
        using (var made = Global.MakePoint(6, 7)!)
            Assert.Equal(6, made.X); // factory pointer return adopted (owned)
    }

    [Fact]
    public void ExceptionTaxonomy()
    {
        var inv = Assert.Throws<ArgumentException>(() => Global.Reject(-1));
        Assert.Equal("negative", inv.Message); // std::invalid_argument
        var ovf = Assert.Throws<ArithmeticException>(() => Global.Reject(1));
        Assert.Equal("too big", ovf.Message); // std::overflow_error
    }

    [Fact]
    public void OperatorsDedicated()
    {
        using var pa = new Point(1, 2);
        using var pb = new Point(3, 4);
        using (var ps = pa + pb)
            Assert.True(ps.X == 4 && ps.Y == 6, "member operator +");
        Assert.True(pa == new Point(1, 2), "operator == compares values");
        Assert.True(pa != pb, "synthesized operator !=");
        Assert.True(pa != null && !(pa == null), "null protocol on ==/!=");
        Assert.True(pa.Equals(new Point(1, 2)), "Equals via ==");
    }

    [Fact]
    public void OperatorsShared()
    {
        using (var va = new operators.Vec(1, 2))
        using (var vb = new operators.Vec(3, 4))
        {
            using (var vs = va + vb)
                Assert.True(vs.X == 4 && vs.Y == 6, "Vec +");
            using (var vn = -va)
                Assert.True(vn.X == -1 && vn.Y == -2, "unary -");
            using (var vm = va * 2.0)
                Assert.Equal(2, vm.X); // Vec * double
            Assert.True(va[0] == 1 && va[1] == 2, "indexer from operator[]");
            Assert.True(va != vb && va == new operators.Vec(1, 2), "Vec ==/!=");
        }
        using (var mt = new operators.Meters(1) + new operators.Feet(10))
            Assert.True(Math.Abs(mt.Value - 4.048) < 1e-9,
                        "heterogeneous welded +");
        using (var cn = new operators.Coin(5) + new operators.Coin(7))
            Assert.Equal(12, cn.Cents); // free anchored +
        using (var mm = new operators.Mixed(1) + new operators.Mixed(2))
            Assert.Equal(3, mm.V); // member + free in one slot (member)
        using (var mi = new operators.Mixed(1) + 41)
            Assert.Equal(42, mi.V); // member + free in one slot (free, int rhs)
        using (var sc = new operators.Scaled(2.5))
        {
            Assert.Equal("Scaled(2.5)", sc.ToString()); // ostream <<
            using (var sr = 2.0 * sc)
                Assert.True(Math.Abs(sr.F - 5.0) < 1e-12, "reflected operand *");
        }
        Assert.True(new operators.Version(1, 2) < new operators.Version(1, 3),
                    "defaulted <=> synthesizes <");
        Assert.True(new operators.Version(1, 2) == new operators.Version(1, 2),
                    "defaulted <=> implicit ==");
        Assert.True(new operators.Temp(1) < new operators.Temp(2) &&
                        new operators.Temp(2) >= new operators.Temp(2),
                    "custom <=> relationals");
        using (var acct = new operators.Account(3))
        {
            Assert.True(acct < 5 && acct == 3 && acct != 4,
                        "heterogeneous <=> + ==");
            Assert.True(2 < acct && 3 <= acct, "reversed heterogeneous <=>");
        }
    }

    [Fact]
    public void InheritanceDedicated()
    {
        using var dog = new Dog();
        Assert.True(dog.Bark() == "woof" && dog.Kind() == "animal",
                    "inherited base method via C# base class");
        dog.Age = 7;
        Assert.Equal(7, Global.AgeOf(dog)); // derived passes as base param
        using (var lg = dog.AsLegged())
            Assert.Equal(4, lg.Legs); // extra base surface via As-view
        Assert.True(dog is Animal, "C# base clause (is Animal)");
    }

    [Fact]
    public void InheritanceShared()
    {
        using (var d = new inheritance.Derived())
        {
            Assert.True(d.BaseMethod() == 1 && d.DerivedMethod() == 2,
                        "welded base chain");
            Assert.True(d is inheritance.Base, "issubclass equivalent");
        }
        using (var leaf = new inheritance.Leaf())
            Assert.True(
                leaf.LeafField == 6 && leaf.MidField == 5 && leaf.BaseField == 1,
                "multi-level chain fields");
        using (var wm = new inheritance.WithMixin())
            Assert.True(wm.OwnField == 4 && wm.MixinMethod() == 3,
                        "non-welded mixin flattened");
        using (var th = new inheritance.Through())
            Assert.True(th.ThroughField == 12 && th.WeldedMethod() == 10 &&
                            th is inheritance.Welded,
                        "welded base through a non-welded bridge");
        using (var bot = new inheritance.Bottom())
        {
            Assert.True(bot is inheritance.Left && bot is inheritance.Apex,
                        "diamond primary chain");
            using (var r = bot.AsRight())
                Assert.True(r is inheritance.Right, "diamond extra base As-view");
        }
    }

    [Fact]
    public void RetpolicyShared()
    {
        using var owner = new retpolicy.Owner();
        var v = owner.View(); // reference_internal (shared case)
        v.V = 9;
        Assert.Equal(9, owner.InnerV()); // view aliases
        using (var s2 = owner.Snapshot())
        {
            s2.V = 1;
            Assert.Equal(9, owner.InnerV()); // snapshot copies
        }
    }

    [Fact]
    public void ValueContainers()
    {
        using (var bk = new Basket())
        {
            Assert.True(bk.Nums.Length == 3 && bk.Nums[2] == 3,
                        "vector<int> field -> int[] copy");
            bk.Nums = new[] { 5, 6 };
            Assert.Equal(12, bk.Total(new[] { 1 })); // vector param + field set
            Assert.Equal(1, bk.Find(6));             // optional<int> some
            Assert.Null(bk.Find(9));                 // optional<int> none
            Assert.Null(bk.Label); // optional<string> field (empty)
            bk.Label = "tag";
            Assert.Equal("tag", bk.Label);
            var t = bk.Triple();
            Assert.True(t.Length == 3 && Math.Abs(t[1] - 2.5) < 1e-12,
                        "array<double,3> return");
            bk.SetTriple(new[] { 1.0, 2.0, 3.0 });
            Assert.True(Math.Abs(bk.TripSum() - 6.0) < 1e-12,
                        "array<double,3> param");
            // wrong extent -> ArgumentException (std::invalid_argument)
            Assert.ThrowsAny<ArgumentException>(() => bk.SetTriple(new[] { 1.0 }));
        }
        using (var mp = Global.MaybePoint(true)!)
            Assert.Equal(3, mp.X); // optional<welded> some (owned copy)
        Assert.Null(Global.MaybePoint(false)); // optional<welded> none -> null
        Assert.Equal(Level.High, Global.MaybeLevel(true));
        Assert.Null(Global.MaybeLevel(false)); // optional<enum>
    }

    [Fact]
    public void PairsAndTuples()
    {
        var (n, name) = Global.Tagged(4);
        Assert.True(n == 4 && name == "n4", "pair return -> ValueTuple");
        Assert.Equal(9, Global.PairSum((4, 5L))); // pair param
        var (i, d, s, pt) = Global.Bundle();
        Assert.True(i == 7 && Math.Abs(d - 2.5) < 1e-12 && s == "seven",
                    "tuple return (leaf elements)");
        using (pt)
            Assert.Equal(1, pt.X); // a welded element crosses as an owned copy
    }

    [Fact]
    public void NestedMemberTypes()
    {
        using var mc = new Machine();
        Assert.Equal(Machine.State.Off, mc.Power); // nested enum type + field
        mc.TurnOn();
        Assert.Equal(Machine.State.On, mc.Power);
        mc.Dial.Value = 42; // nested class field is a live view
        Assert.Equal(42, mc.Dial.Value);
        using (var g = new Machine.Gauge())
            Assert.Equal(0, g.Value); // nested class constructible from C#
        using (var pk = mc.Peak())
            Assert.Equal(99, pk.Value); // nested class return (owned copy)
    }

    [Fact]
    public void ReferenceVectors()
    {
        using var rt = new Route();
        var stops = rt.Stops; // a live wrapper over the member
        using (var p1 = new Point(1, 2))
            stops.Add(p1);
        using (var p2 = new Point(3, 4))
            stops.Add(p2);
        Assert.Equal(2, rt.StopCount()); // Add writes through
        stops[0].X = 10;                 // a live element view
        Assert.Equal(13, rt.TotalX(stops));
        using (var rev = rt.Reversed())
            Assert.True(rev.Count == 2 && rev[0].X == 3,
                        "owned vector return + indexer");
        stops.Clear();
        Assert.Equal(0, rt.StopCount()); // Clear writes through
        using (var mine = new VectorPoint())
        {
            using (var p = new Point(7, 0))
                mine.Add(p);
            Assert.Equal(7, rt.TotalX(mine)); // C#-constructed vector as param
            // bounds-checked at() -> ArgumentOutOfRangeException
            Assert.Throws<ArgumentOutOfRangeException>(() => _ = mine[5]);
        }
    }

    [Fact]
    public void ResolutionShared()
    {
        using var sh = new resolution.Shielded();
        Assert.Equal(42, sh.Total());
        Assert.Equal(40, sh.Base());              // protected method bound
        Assert.Equal(6, sh.Scale(3));             // protected overloads
        Assert.Equal(1.0, sh.Scale(0.5), 12);
        Assert.Equal(7, resolution.Shielded.Origin()); // protected static
        sh.Boost = 5;                             // protected data r/w
        Assert.Equal(5, sh.Boost);
    }

    [Fact]
    public void TemplatesShared()
    {
        using (var ic = new templates_ns.IntCrate())
        {
            ic.Put(9);
            Assert.Equal(9, ic.Get()); // alias-welded instantiation
            ic.Item = 11;
            Assert.Equal(11, ic.Item);
        }
        using (var wc = new templates_ns.WordCrate())
        {
            wc.Put("box");
            Assert.Equal("box", wc.Get()); // a second instantiation, own name
        }
        using (var rc = new templates_ns.CrateOfDouble())
            rc.Put(1.5); // weld_as on the alias names it verbatim
        using (var ti = new templates_ns.TaggedBox())
            Assert.Equal(0, ti.Tag); // weld_as on the TEMPLATE
        using (var ip = new templates_ns.IntPack())
        {
            Assert.Equal(0, ip.Unwrap()); // third-party template via alias weld
            using (var tw = ip.Twin())
                Assert.Equal(0, tw.Unwrap()); // the trust-vouched signature
            using (var lid = new templates_ns.IntPack.Lid())
                Assert.Equal(1, lid.Fits); // nested type of the instantiation
        }
        using (var si = new templates_ns.IntSilo())
        {
            Assert.Equal(templates_ns.IntSilo.State.shut,   // enumerators
                         si.Flip(templates_ns.IntSilo.State.open)); // verbatim
            si.Door.Width = 6; // nested class member (cs weld_as: the
                               // hatch/Hatch CS0102 escape), live view
            Assert.Equal(6, si.Door.Width);
        }
        using (var v = new templates_ns.IntVault())
        {
            v.Stash(21); // weld_protected on the template, via instantiation
            Assert.Equal(21, v.Peek());
            Assert.Equal(21, v.Locked); // protected data bound
        }
    }

    [Fact]
    public void CopyingShared()
    {
        using (var sheet = new copying.Sheet())
        {
            sheet.Width = 5;
            using (var c = sheet.Clone())
                Assert.Equal(5, c.Width); // implicit copy ctor rides along
        }
        // deleted / excluded copy ctor -> no Clone() surface at all
        Assert.Null(typeof(copying.Pinned).GetMethod("Clone"));
        Assert.Null(typeof(copying.Sealed).GetMethod("Clone"));
        // ...but a PY-scoped exclude leaves cs's Clone intact
        Assert.NotNull(typeof(copying.PyBlocked).GetMethod("Clone"));
        using (var ch = new copying.Choosy())
            using (var c = ch.Clone())
                Assert.Equal(3, c.Chosen); // opt_in keeps a declared copy ctor
        Assert.NotNull(typeof(copying.Shifty).GetMethod("Clone")); // move ctor skipped
    }

    [Fact]
    public void CovariantPointerDirectors()
    {
        using (var pl = new overridable.Plant())
            Assert.True(pl.Orphan()); // base behavior: parent() == nullptr
        using (var rooted = new RootedTreeCs())
            Assert.False(rooted.Orphan()); // C# override returns an object ->
                                           // C++ sees a non-null Plant*
        using (var lone = new LoneTreeCs())
            Assert.True(lone.Orphan()); // C# override returns null
    }

    [Fact]
    public void DirectorsDedicated()
    {
        using (var sh = new Shape())
        {
            Assert.Equal("shape", sh.Name()); // director base behaves as base
            Assert.Equal("shape:0", sh.Describe());
        }
        using (var sq = new SquareCs())
        {
            Assert.Equal("square", sq.Name()); // C# override called directly
            Assert.Equal("square:4", sq.Describe()); // C++ dispatch reaches C#
            Assert.Equal("square", Global.DescribeShape(sq)); // base-typed param
        }
        using (var bc = new BaseCallerCs())
            Assert.Equal("mega-shape:0", bc.Describe()); // base.Name() terminates
        using (var th = new ThrowerCs())
        {
            var ex = Assert.Throws<WelderNativeException>(() => th.Describe());
            Assert.True(ex.NativeCode == 7 && ex.Message == "cs-boom",
                        "managed exception round-trips as code 7");
        }
    }

    [Fact]
    public void DirectorsShared()
    {
        using (var an = new overridable.Animal())
        {
            Assert.Equal("... on 4 legs", an.Describe()); // unoverridden
            Assert.Equal("Animalia", an.Kingdom()); // bind_flat plain method
        }
        using (var ld = new LoudDogCs())
            Assert.Equal("WOOF on 2 legs", ld.Describe()); // C++ -> C# dispatch
        using (var bird = new CsBird())
            Assert.True(bird.Describe() == "tweet on 2 legs" &&
                            bird.Fly() == "soar",
                        "inherited + own virtual slots on a derived director");
        using (var cy = new CyborgCs())
        {
            Assert.Equal("STEP STEP ", cy.March(2)); // parameterful override
            Assert.Equal("INT:7|STR:hi", cy.Transmit()); // overloaded per slot
            Assert.Equal(500, cy.Recharge(5)); // noexcept override
            Assert.Equal("proto=asimov", cy.Handshake()); // unbound NVI -> base
        }
    }

    [Fact]
    public void ReferenceMaps()
    {
        using var d = new Depot();
        var sites = d.Sites; // a live view of the std::map member
        Assert.Equal(0, sites.Count);
        using (var hq = new Point(2, 3))
            sites["hq"] = hq; // insert-or-assign copies the point in
        Assert.True(sites.ContainsKey("hq"));
        Assert.Equal(1, d.SiteCount()); // the C++ member saw the write
        sites["hq"].X = 40; // element view writes through
        Assert.Equal(40, d.Sites["hq"].X);
        Assert.Throws<ArgumentOutOfRangeException>(() => sites["nowhere"]);
        Assert.True(sites.Remove("hq"));
        Assert.False(sites.Remove("hq"));
        Assert.Equal(0, d.SiteCount());

        var labels = d.Labels; // unordered_map with leaf key/value
        labels[7] = "seven";
        labels[35] = "thirty-five";
        Assert.True(labels.Count == 2 && labels[7] == "seven",
                    "leaf-valued umap roundtrip");
        Assert.Equal(42, d.LabelKeys(labels)); // a map as a parameter
        labels.Clear();
        Assert.Equal(0, labels.Count);

        using var standalone = new MapStrPoint(); // wrapper-owned fresh map
        using (var p = new Point(1, 1))
            standalone["a"] = p;
        Assert.Equal(1, standalone.Count);
    }

    [Fact]
    public void FixedArrayOfWelded()
    {
        using var c = new Cable();
        var ends = c.Ends; // a live, fixed-size view
        Assert.Equal(2, ends.Count);
        ends[0].X = 5; // element view writes through
        ends[1].X = 30;
        Assert.Equal(25, c.SpanX());
        using (var p = new Point(100, 0))
            ends[1] = p; // whole-element assignment
        Assert.Equal(95, c.SpanX());
        Assert.Throws<ArgumentOutOfRangeException>(() => ends[2]);
    }

    [Fact]
    public void SmartPointers()
    {
        var sp = Global.SharedPoint(6, 7); // shared_ptr -> pinned view
        Assert.NotNull(sp);
        Assert.Equal(6, sp!.X);
        Assert.Equal(6, Global.SharedX(sp)); // borrowed back in, not adopted
        Assert.Equal(13, sp.Sum()); // still alive after the borrow
        Assert.Null(Global.NoPoint()); // a null shared_ptr -> null

        var up = Global.UniquePoint(8, 9); // unique_ptr -> ownership transfer
        Assert.NotNull(up);
        Assert.Equal(17, up!.Sum());
        up.Dispose(); // the C# side owns and frees it
        Assert.Equal(-1, Global.SharedX(null)); // null borrow -> C++ nullptr
    }
}

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

class RootedTreeCs : overridable.Tree
{
    private readonly overridable.Tree _parent = new overridable.Tree();
    public override overridable.Tree? Parent() => _parent;
}

class LoneTreeCs : overridable.Tree
{
    public override overridable.Tree? Parent() => null;
}

class CyborgCs : overridable.Robot
{
    public override string Obey(string order, int times)
    {
        string r = "";
        for (int i = 0; i < times; i++)
            r += order.ToUpperInvariant();
        return r;
    }
    public override string Send(int code) => "INT:" + code;
    public override string Send(string text) => "STR:" + text;
    public override int Recharge(int amount) => amount * 100;
}
