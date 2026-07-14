"""Cookbook 04 — assert Python-side overriding of C++ virtuals (`robots`)."""

import robots


class Scout(robots.Robot):
    def name(self) -> str:
        return "scout-7"

    def speed(self) -> int:
        return 9


class Survey(robots.Mission):
    def duty(self) -> str:
        return "map the crater"


def main() -> None:
    # The C++ defaults still work.
    base = robots.Robot()
    assert base.status() == "unit @ speed 1"
    assert base.vendor() == "ACME"  # bind_flat: callable...

    # A Python subclass overrides the virtuals; the NON-virtual C++ status()
    # calls back into the Python overrides — real C++ -> Python dispatch.
    s = Scout()
    assert s.status() == "scout-7 @ speed 9"

    # bind_flat methods are not routed through the trampoline: a Python override
    # is invisible to C++ callers (and that's the point — no dispatch cost).
    class Rebrand(robots.Robot):
        def vendor(self) -> str:
            return "python-inc"

    assert robots.Robot.vendor(Rebrand()) == "ACME"

    # The abstract base: a Python subclass supplies the pure virtual and C++
    # dispatches into it; instantiating the base and calling the unoverridden
    # pure virtual raises at CALL time (framework behavior).
    m = Survey()
    assert m.briefing() == "mission: map the crater"
    try:
        robots.Mission().briefing()
        raise AssertionError("unoverridden pure virtual should raise")
    except RuntimeError:
        pass

    print("cookbook 04-virtuals: OK")


if __name__ == "__main__":
    main()