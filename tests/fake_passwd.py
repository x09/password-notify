#!/usr/bin/python3
# Offline passwd simulator: no PAM, network or actual password updates.
import os, sys, time, termios
assert os.isatty(0) and os.environ['LC_ALL'] == 'C'
tty = open('/dev/tty', 'r')
mode = os.environ.get('PASSWORD_NOTIFY_TEST_MODE', 'success')
if mode == 'timeout':
    time.sleep(3)
    sys.exit(1)
if mode == 'unknown':
    sys.stdout.write('Token password: '); sys.stdout.flush()
    tty.readline(); sys.exit(1)
def ask(prompt, expected):
    assert not termios.tcgetattr(0)[3] & termios.ECHO
    # Exercise prompts split across reads with no trailing newline.
    for part in (prompt[:7], prompt[7:]):
        sys.stdout.write(part); sys.stdout.flush(); time.sleep(.02)
    assert tty.readline().rstrip('\n') == expected
ask('(current) UNIX password: ', 'dummy-old')
ask('(current) NT password: ', 'dummy-old')
if mode == 'auth':
    print('\nAuthentication failure'); sys.exit(1)
ask('Enter new NT password: ', 'dummy-new')
if mode == 'policy':
    print('\nBAD PASSWORD: too short'); sys.exit(1)
ask('Retype new NT password: ', 'dummy-new')
if mode == 'long':
    print('\n' + 'server diagnostic ' * 40 + ' FINAL SERVER REJECTION'); sys.exit(1)
if mode == 'redact':
    print('\nRejected dummy-old and dummy-new'); sys.exit(1)
print('\npasswd: password updated successfully')
