import drs_bridge


def test_package_importable() -> None:
    assert drs_bridge.__version__ == "0.1.0"
