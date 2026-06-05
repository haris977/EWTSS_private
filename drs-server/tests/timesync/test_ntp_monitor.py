import pytest
from drs_server.timesync.ntp_monitor import NtpMonitor, NtpSample


@pytest.mark.asyncio
async def test_parse_ntpq_output_extracts_offset_jitter_stratum():
    sample_output = """assID=0 status=0615 leap_none, sync_ntp, 1 event, clock_sync,
version="ntpd 4.2.8p18", processor="x86_64", system="Windows 11",
leap=00, stratum=2, precision=-23, rootdelay=1.234, rootdisp=2.345,
refid=192.168.1.10, reftime=0xeb1d2a1c.5e76f2c0, clock=0xeb1d2a1c.9876fedc,
peer=12345, tc=6, mintc=3, offset=0.412, frequency=-1.234, sys_jitter=0.187,
clk_jitter=0.234, clk_wander=0.012"""

    monitor = NtpMonitor(ntpq_path="C:\\Program Files\\NTP\\bin\\ntpq.exe")
    sample = monitor._parse_rv_output(sample_output)
    assert sample.offset_ms == pytest.approx(0.412, rel=1e-3)
    assert sample.jitter_ms == pytest.approx(0.187, rel=1e-3)
    assert sample.stratum == 2


@pytest.mark.asyncio
async def test_parse_ntpq_output_raises_on_unparseable():
    monitor = NtpMonitor(ntpq_path="dummy")
    with pytest.raises(ValueError, match="Could not parse"):
        monitor._parse_rv_output("garbage output")
