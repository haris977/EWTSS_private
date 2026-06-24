cd C:\Users\Admin\drs-bridge-test
.\.venv\Scripts\python -c "from drs_bridge.main import main; main()" 2>&1 | Tee-Object -FilePath C:\Users\Admin\drs-bridge-test\bridge_out.txt
