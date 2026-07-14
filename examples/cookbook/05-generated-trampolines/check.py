"""Cookbook 05 — assert the GENERATED trampolines behave like hand-written ones."""

import machines


class Vtec(machines.Engine):
    def hum(self) -> str:
        return "vtec"

    def rpm(self) -> int:
        return 9000


class BigTurbo(machines.Turbo):
    # Overrides one INHERITED virtual and the own one — both routed through the
    # generated trampoline of Turbo itself.
    def rpm(self) -> int:
        return 7000

    def boost(self) -> int:
        return 5


class Solar(machines.Generator):
    def watts(self) -> int:
        return 5000


def main() -> None:
    assert machines.Engine().report() == "brrr @ 800"

    # C++ (report) dispatches into the Python overrides via the generated trampoline.
    assert Vtec().report() == "vtec @ 9000"

    # bind_flat stayed flat: callable, but not overridable from Python.
    class Sticker(machines.Engine):
        def label(self) -> str:
            return "custom"

    assert machines.Engine.label(Sticker()) == "engine"

    # The derived type's generated trampoline covers inherited + own virtuals.
    b = BigTurbo()
    assert b.report() == "brrr @ 7000"  # inherited hum, overridden rpm
    assert b.boost() == 5

    # The abstract base is implementable from Python (the generated trampoline is
    # the construction type); C++ dispatches into the implementation.
    assert Solar().kilowatts() == 5

    print("cookbook 05-generated-trampolines: OK")


if __name__ == "__main__":
    main()