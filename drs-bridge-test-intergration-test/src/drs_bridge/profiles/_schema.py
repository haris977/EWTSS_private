"""Pydantic schema for variant YAML profiles.

Per B1.3 design spec §5.3 — extends the profile with a `time_signal` block
that drs-bridge reads at startup to know which time-distribution pattern
each variant uses.
"""

from typing import Literal, Optional

from pydantic import BaseModel, Field, model_validator


class PortConfig(BaseModel):
    host: str = "0.0.0.0"
    port: int
    protocol: Literal["tcp", "udp"]


class PeriodicDistribution(BaseModel):
    enabled: bool
    interval_ms: Optional[int] = None


class TimeSignalConfig(BaseModel):
    embedded_in_messages: bool
    periodic_distribution: PeriodicDistribution
    precision_required_ms: float = Field(gt=0.0)

    @model_validator(mode="after")
    def periodic_requires_interval(self) -> "TimeSignalConfig":
        if self.periodic_distribution.enabled and self.periodic_distribution.interval_ms is None:
            raise ValueError("interval_ms must be set when periodic_distribution.enabled is true")
        return self


class VariantProfile(BaseModel):
    variant: str
    parser_lib: str
    time_signal: TimeSignalConfig


class RosterEntry(BaseModel):
    instance_id: str
    variant: str
    host: str
    command: PortConfig
    response: PortConfig
    port_source: Literal["irs_fixed", "allocated"]
    enabled: bool


class Roster(BaseModel):
    roster_id: str
    version: int = Field(ge=1)
    entries: list[RosterEntry]

    @property
    def etag(self) -> str:
        return f"{self.roster_id}@{self.version}"
