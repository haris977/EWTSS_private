"""Scans a directory for `*.yaml` variant profiles and parses each into a
VariantProfile. The runtime calls this once at startup.
"""
from pathlib import Path

import yaml

from drs_bridge.profiles._schema import VariantProfile


def load_profiles(profiles_dir: Path) -> dict[str, VariantProfile]:
    """Return {variant_name: VariantProfile} for every *.yaml under profiles_dir.

    Raises pydantic.ValidationError if any file fails the schema.
    """
    out: dict[str, VariantProfile] = {}
    for path in sorted(profiles_dir.glob("*.yaml")):
        with path.open(encoding="utf-8") as f:
            raw = yaml.safe_load(f)
        profile = VariantProfile(**raw)
        out[profile.variant] = profile
    return out


def load_commands(profiles_dir: Path) -> dict[str, dict[str, str]]:
    """Return {variant: {"group/unit": command_name}} from the optional
    ``commands:`` section in each YAML profile (ignored by pydantic schema).
    """
    out: dict[str, dict[str, str]] = {}
    for path in sorted(profiles_dir.glob("*.yaml")):
        with path.open(encoding="utf-8") as f:
            raw = yaml.safe_load(f)
        variant = raw.get("variant", "")
        cmds = raw.get("commands") or {}
        if variant and cmds:
            out[variant] = {str(k): str(v) for k, v in cmds.items()}
    return out
