"""Cookbook 02 — assert the discovery rules of the `inventory` module."""

import inventory


def main() -> None:
    # The namespace doc became the module docstring; the post-glue attribute is set.
    assert "warehouse inventory" in inventory.__doc__
    assert inventory.SCHEMA_VERSION == 2

    # automatic policy: public members bind, the excluded one does not.
    item = inventory.Item()
    item.name, item.quantity = "bolts", 3
    assert not item.depleted()
    assert not hasattr(item, "cache_key")

    # weld_as: the verbatim rename is the ONLY exposed spelling.
    assert hasattr(inventory, "Crate")
    assert not hasattr(inventory, "BigWoodenBox")
    assert inventory.Crate().capacity == 100.0

    # No weld marker -> never discovered.
    assert not hasattr(inventory, "Ledger")

    # opt_in policy: only the included members bind.
    audit = inventory.AuditRecord()
    audit.note, audit.revision = "checked", 7
    assert not hasattr(audit, "internal_id")

    # Free functions: docstring, and overloads dispatch under one name.
    assert inventory.totalValue(item, 2.5) == 7.5
    inventory.restock(item, 5)
    inventory.restock(item)  # the no-amount overload adds 10
    assert item.quantity == 18

    # Nested namespaces: `pricing` became a submodule; `detail` was pruned even
    # though its contents carry weld markers.
    assert inventory.pricing.discounted(200.0, 25.0) == 150.0
    assert not hasattr(inventory, "detail")

    print("cookbook 02-discovery: OK")


if __name__ == "__main__":
    main()