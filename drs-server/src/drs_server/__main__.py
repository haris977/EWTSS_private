"""Allow `python -m drs_server` to launch the uvicorn server."""
import uvicorn

from drs_server.config import ServerSettings


def main() -> None:
    settings = ServerSettings()
    uvicorn.run(
        "drs_server.main:app",
        host=settings.host,
        port=settings.port,
        log_level=settings.log_level.lower(),
    )


if __name__ == "__main__":
    main()
