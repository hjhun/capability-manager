#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run an owned build command with a deadline and process-group cleanup."""
import argparse
import os
import signal
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds', type=float, required=True)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command or args.seconds <= 0:
        parser.error('positive --seconds and a command are required')
    try:
        process = subprocess.Popen(command, start_new_session=True)
        deadline = time.monotonic() + args.seconds
        timed_out = False
        while True:
            status = os.waitid(os.P_PID, process.pid, os.WEXITED | os.WNOHANG | os.WNOWAIT)
            if status is not None:
                break
            if time.monotonic() >= deadline:
                timed_out = True
                break
            time.sleep(min(0.02, max(0, deadline - time.monotonic())))
        # Keep the leader unreaped until group cleanup, including normal exit.
        # A shell can exit with descendants still holding files/pipes in the scope.
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        code = process.wait()
        if timed_out:
            print('Owned command exceeded its deadline', file=sys.stderr)
            return 124
        return code if code >= 0 else 128 - code

    except OSError as error:
        print(str(error), file=sys.stderr)
        return 127


if __name__ == '__main__':
    sys.exit(main())
