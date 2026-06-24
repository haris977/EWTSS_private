import pytest
from pydantic import ValidationError

from drs_bridge.profiles._schema import Roster, RosterEntry


def _entry(instance_id="rdfs#1", variant="rdfs", port=5001, enabled=True):
    return {
        "instance_id": instance_id,
        "variant": variant,
        "host": "127.0.0.1",
        "command": {"port": port, "protocol": "tcp"},
        "response": {"port": port + 1, "protocol": "udp"},
        "port_source": "irs_fixed",
        "enabled": enabled,
    }


def test_roster_parses_valid_snapshot():
    roster = Roster(roster_id="lab-full", version=7, entries=[_entry()])
    assert roster.roster_id == "lab-full"
    assert roster.version == 7
    assert roster.entries[0].instance_id == "rdfs#1"
    assert roster.entries[0].command.protocol == "tcp"


def test_roster_entry_rejects_bad_protocol():
    with pytest.raises(ValidationError):
        RosterEntry(**{**_entry(), "command": {"port": 5001, "protocol": "sctp"}})


def test_roster_requires_version():
    with pytest.raises(ValidationError):
        Roster(roster_id="x", entries=[])
