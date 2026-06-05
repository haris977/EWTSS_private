"""Sanity checks that the project's top-level imports resolve."""


def test_czml3_importable():
    import czml3
    assert czml3 is not None


def test_stk_service_interface_importable():
    from stk_service import IStkService
    assert IStkService is not None


def test_scenario_builder_importable_without_agi():
    """scenario_builder defers ``from agi.stk12.stkobjects import …`` to call time,
    so the module itself should import even when agi.stk12 is blocked by conftest."""
    import scenario_builder
    assert callable(scenario_builder.build_stk_scenario)
