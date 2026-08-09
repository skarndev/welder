// The C# consumer: a plain console app over the generated Bindings.cs + native
// library (no test-framework dependency — a failed assert throws, CTest sees
// the non-zero exit). Run via `dotnet run` by the cookbook.11-csharp test.
using System;
using inventory;

static class Check
{
    static void Require(bool cond, string what)
    {
        if (!cond)
            throw new Exception("FAILED: " + what);
    }

    static int Main()
    {
        using (var crate = new Crate("bolts", 12))
        {
            Require(crate.Label == "bolts" && crate.Weight == 12,
                    "ctor + field properties");

            var serials = crate.Serials;       // a live view of the C++ member
            serials.Add(100);
            serials.Add(101);
            Require(crate.SerialTotal() == 201, "live container writes through");

            var span = serials.AsSpan();       // zero-copy over the C++ buffer
            span[0] = 500;
            Require(crate.SerialTotal() == 601, "span writes are zero-copy");

            using var other = new Crate("nuts", 8);
            Require(Global.CombinedWeight(crate, other) == 20,
                    "root free function on Global");
            Require(inventory.Audit.Global.Stamp(other) == "nuts#8",
                    "nested namespace -> inventory.Audit.Global");
        }
        Console.WriteLine("cookbook 11-csharp: OK");
        return 0;
    }
}
