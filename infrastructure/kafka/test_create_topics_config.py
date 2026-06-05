"""Unit check: drs.roster is declared as a compacted topic."""
import importlib.util
from pathlib import Path

_MOD_PATH = Path(__file__).resolve().parent / "create-topics.py"
_spec = importlib.util.spec_from_file_location("create_topics", _MOD_PATH)
create_topics = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(create_topics)


def test_drs_roster_is_compacted():
    by_name = {t[0]: t for t in create_topics.CONTROL_PLANE_TOPICS}
    assert "drs.roster" in by_name, "drs.roster must be provisioned"
    name, parts, rf, config = by_name["drs.roster"]
    assert config.get("cleanup.policy") == "compact"
    assert parts == 1, "compacted active-roster topic is single-partition"
