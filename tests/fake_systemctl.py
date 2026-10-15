#!/usr/bin/python3
"""Offline scheduler tests: record argv, never contact or modify systemd."""
import json, os, sys
with open(os.environ['TEST_SYSTEMCTL_LOG'], 'a') as output:
    output.write(json.dumps(sys.argv[1:]) + '\n')
if os.environ.get('TEST_SYSTEMCTL_FAIL'):
    print('test user manager unavailable', file=sys.stderr)
    sys.exit(1)

if "is-active" in sys.argv:
    sys.exit(0 if os.environ.get("TEST_SYSTEMCTL_ACTIVE") else 3)
