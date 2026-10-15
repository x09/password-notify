"""Run under xvfb-run. Tests GTK while mock LDAP blocks for 30 seconds."""
import subprocess
import signal
import time
import sys
import os
import tempfile
from pathlib import Path

test_runtime = tempfile.TemporaryDirectory(prefix="password-notify-ui-")
log = Path(test_runtime.name) / "systemctl.jsonl"
env = os.environ | {"XDG_RUNTIME_DIR": test_runtime.name, "TEST_SYSTEMCTL_LOG": str(log), "LC_ALL": "C", "LANGUAGE": ""}
for sig in (signal.SIGINT, signal.SIGTERM):
    p = subprocess.Popen([sys.argv[1], "-s", "-d"], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    time.sleep(1)
    start = time.monotonic()
    p.send_signal(sig)
    try:
        out, err = p.communicate(timeout=2)
    except subprocess.TimeoutExpired:
        p.kill()
        p.communicate()
        raise AssertionError("GTK did not exit while LDAP worker was blocked")
    assert p.returncode == 0, (p.returncode, err)
    assert err.count("Checking password expiration") == 1, err
    assert err.count("TEST: blocked network worker started") == 1, err
    assert "Shutting down" in err, err
    print(f"PASS: one startup check; {sig.name} exits during blocked LDAP in {time.monotonic()-start:.3f}s")

test_runtime.cleanup()
