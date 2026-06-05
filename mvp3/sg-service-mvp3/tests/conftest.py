import sys
import pytest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent.parent))


@pytest.fixture(autouse=True, scope="session")
def block_agi_stk12():
    """Force the MockStkService fallback in tests by hiding agi.stk12 imports."""
    sys.modules["agi"] = None
    sys.modules["agi.stk12"] = None
    yield
    sys.modules.pop("agi", None)
    sys.modules.pop("agi.stk12", None)
