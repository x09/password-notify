"""Run under xvfb-run: all LDAP and systemctl interactions are mocked."""
import os, sys, tempfile, subprocess, json, time, signal
from pathlib import Path
binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix='password-notify-scheduler-') as temp:
    base = Path(temp)
    runtime = base / 'runtime'
    runtime.mkdir(mode=0o700)
    config = base / 'config % $ test.ini'
    config.write_text('[general]\nwarn_days = 7\ncheck_interval_hours = 3\n')
    log = base / 'calls.jsonl'
    env = os.environ | {'XDG_RUNTIME_DIR': str(runtime), 'TEST_SYSTEMCTL_LOG': str(log), 'LC_ALL': 'C', 'LANGUAGE': ''}
    def calls():
        return [json.loads(line) for line in log.read_text().splitlines()] if log.exists() else []
    def run(state, *args):
        try:
            return subprocess.run([binary, *args, '-d', '-c', str(config)], env=env | {'TEST_EXPIRY_STATE': state}, capture_output=True, text=True, timeout=5)
        except subprocess.TimeoutExpired as exc:
            print('Timed out state:', state, 'stderr:', exc.stderr, flush=True)
            raise
    for state in ('40', 'never', 'error'):
        log.write_text('')
        result = run(state, '-s')
        assert result.returncode == (1 if state == 'error' else 0), (state, result.stderr)
        assert ['--user', '--no-block', 'restart', 'password-notify-check.timer'] in calls()
        assert not any('stop' in c for c in calls()), calls()
    timer = runtime / 'systemd/user/password-notify-check.timer'
    service = runtime / 'systemd/user/password-notify-check.service'
    assert 'OnActiveSec=3h' in timer.read_text() and 'OnUnitInactiveSec=3h' in timer.read_text()
    assert 'config %% $$ test.ini' in service.read_text()
    assert 'ExecStopPost=' in service.read_text() and ' -u ' in service.read_text()
    hook = run('40', '-u'); assert hook.returncode == 0 and 'Checking password expiration' not in hook.stderr
    config.write_text('[general]\nwarn_days = 7\ncheck_interval_hours = 5\n')
    result = run('40', '-s'); assert result.returncode == 0
    assert 'OnActiveSec=5h' in timer.read_text()
    assert len(list(timer.parent.glob('password-notify-check.timer'))) == 1
    verified = subprocess.run(['systemd-analyze', 'verify', str(service), str(timer)], capture_output=True, text=True)
    assert verified.returncode == 0, verified.stderr
    log.write_text('')
    result = run('40', '-a'); assert result.returncode == 0, result.stderr
    assert any('import-environment' in c for c in calls())
    assert ['--user', '--no-block', 'start', 'password-notify-check.service'] in calls()
    log.write_text('')
    active = subprocess.run([binary, '-a', '-c', str(config)], env=env | {'TEST_SYSTEMCTL_ACTIVE': '1'}, capture_output=True, text=True, timeout=5)
    assert active.returncode == 0
    assert ['--user', '--no-block', 'stop', 'password-notify-check.timer'] in calls()
    assert not any('restart' in c for c in calls())
    log.write_text('')
    p = subprocess.Popen([binary, '-s', '-d', '-c', str(config)], env=env | {'TEST_EXPIRY_STATE': '2'}, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        deadline = time.monotonic() + 5
        while not any('stop' in c for c in calls()) and time.monotonic() < deadline:
            assert p.poll() is None
            time.sleep(.05)
        assert ['--user', '--no-block', 'stop', 'password-notify-check.timer'] in calls()
        assert p.poll() is None, 'warning must keep the tray process running'
        second = run('40', '-s'); assert second.returncode == 0
        assert 'Checking password expiration' not in second.stderr, second.stderr
        p.send_signal(signal.SIGTERM)
        out, err = p.communicate(timeout=3)
        assert p.returncode == 0, err
        assert ['--user', '--no-block', 'restart', 'password-notify-check.timer'] in calls()
    finally:
        if p.poll() is None: p.kill(); p.communicate()
    headless = subprocess.run([binary, '-a'], env=env | {'DISPLAY': '', 'WAYLAND_DISPLAY': ''}, capture_output=True, text=True, timeout=5)
    assert headless.returncode != 0
    # A missing user manager is an error, never silently reported as success.
    bad = subprocess.run([binary, '-a', '-c', str(config)], env=env | {'TEST_SYSTEMCTL_FAIL': '1'}, capture_output=True, text=True, timeout=5)
    assert bad.returncode != 0
    print('PASS: no-warning/never/error exit; warning keeps tray; pause/rearm; one timer; interval reload; singleton; escaped config path; session import; manager failure (offline)')
