import importlib
import sys

from stk_mock_service import MockStkService


def test_factory_returns_mock_when_agi_not_installed(monkeypatch):
    """Ensure get_stk_service() falls back to MockStkService if agi.stk12 is absent."""
    # Simulate agi.stk12 being unimportable
    monkeypatch.setitem(sys.modules, "agi", None)
    monkeypatch.setitem(sys.modules, "agi.stk12", None)

    import stk_com_service
    importlib.reload(stk_com_service)

    svc = stk_com_service.get_stk_service()
    assert isinstance(svc, MockStkService)


def test_factory_returns_mock_on_com_connection_failure(monkeypatch):
    """If STK COM raises on attach, factory must still return MockStkService."""
    import types

    fake_stk12 = types.ModuleType("agi.stk12")

    class FakeDesktop:
        @staticmethod
        def AttachToApplication():
            raise OSError("STK is not running")

        @staticmethod
        def StartApplication(**kwargs):
            raise OSError("STK is not installed")

    fake_stk12.stkdesktop = types.SimpleNamespace(STKDesktop=FakeDesktop)

    monkeypatch.setitem(sys.modules, "agi.stk12", fake_stk12)
    monkeypatch.setitem(sys.modules, "agi.stk12.stkdesktop", fake_stk12.stkdesktop)

    import stk_com_service
    importlib.reload(stk_com_service)

    svc = stk_com_service.get_stk_service()
    assert isinstance(svc, MockStkService)
