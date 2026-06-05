from unittest.mock import AsyncMock, patch

import pytest

from drs_bridge import main


@pytest.mark.asyncio
async def test_run_starts_and_stops_runtime(tmp_path):
    fake_runtime = AsyncMock()
    fake_runtime.run_until_stopped = AsyncMock()
    fake_runtime.start = AsyncMock()
    fake_runtime.shutdown = AsyncMock()

    with patch("drs_bridge.main.Runtime", return_value=fake_runtime), patch(
        "drs_bridge.main._install_signal_handlers"
    ) as fake_install:
        await main.run(profiles_dir=tmp_path, kafka_bootstrap="dummy:9092")

    fake_runtime.start.assert_awaited_once()
    fake_runtime.run_until_stopped.assert_awaited_once()
    fake_runtime.shutdown.assert_awaited_once()
    fake_install.assert_called_once()


def test_configure_logging_uses_dict_config():
    with patch("drs_bridge.main.logging.config.dictConfig") as fake_dictcfg:
        main.configure_logging("DEBUG")
    fake_dictcfg.assert_called_once()
