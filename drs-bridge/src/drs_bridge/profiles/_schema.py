"""Pydantic schema for variant YAML profiles.

Per B1.3 design spec §5.3 — extends the profile with a `time_signal` block
that drs-bridge reads at startup to know which time-distribution pattern
each variant uses.
"""
from typing import Literal, Optional

from pydantic import BaseModel, Field, model_validator


class PortConfig(BaseModel):
    host: str
    port: int
    protocol: Literal["tcp", "udp"]


class HttpSourceConfig(BaseModel):
    """Connection config for HTTP-polling variants (e.g. AUS-C2).
    Used instead of parser_lib + ports for REST API sources.
    """
    host: str
    port: int = 5000
    username: str = "admin"
    password: str = "admin"
    poll_interval_s: float = Field(default=1.0, gt=0.0)
    api_version: str = "v4.2"


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
            raise ValueError(
                "interval_ms must be set when periodic_distribution.enabled is true"
            )
        return self


class VariantProfile(BaseModel):
    variant: str
    time_signal: TimeSignalConfig

    # DLL-based variants (binary TCP/UDP protocol)
    parser_lib: Optional[str] = None
    ports: Optional[dict[str, PortConfig]] = None

    # HTTP-polling variants (REST API — e.g. AUS-C2)
    http_source: Optional[HttpSourceConfig] = None
    kafka_topic: Optional[str] = None

    @model_validator(mode="after")
    def source_validation(self) -> "VariantProfile":
        has_dll  = self.parser_lib is not None
        has_http = self.http_source is not None

        if not has_dll and not has_http:
            raise ValueError(
                "At least one of parser_lib or http_source must be set."
            )
        # DLL-only (binary TCP/UDP protocol): requires ports for inbound frames.
        # HTTP+DLL (e.g. AUS-C2): http_source supplies transport, parser_lib
        #   supplies JSON parsing — ports are not used.
        if has_dll and not has_http and self.ports is None:
            raise ValueError("DLL-only variant requires a 'ports' section.")
        return self
