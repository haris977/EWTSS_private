import sys
import pytest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent.parent))


@pytest.fixture(autouse=True, scope="session")
def block_agi_stk13():
    """Force the MockStkService fallback in tests by hiding agi.stk13 imports."""
    sys.modules["agi"] = None
    sys.modules["agi.stk13"] = None
    yield
    sys.modules.pop("agi", None)
    sys.modules.pop("agi.stk13", None)
