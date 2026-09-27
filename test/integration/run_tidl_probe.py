#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run the actual TIDL connection fixture on an explicitly selected native image.

Requires a built capmgr-tidl-probe and real RPC/Cynara services. This verifies
connection authorization only; it does not verify production catalog or mounts.
"""
import argparse
import pathlib
import subprocess
import tempfile
import time
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=pathlib.Path)
    args = parser.parse_args()
    binary = str(args.binary.resolve(strict=True))
    with tempfile.TemporaryDirectory(prefix='capmgr-tidl-probe-') as root:
        root = pathlib.Path(root)
        ready = root / 'ready'
        endpoint = 'd::capmgr.probe.' + uuid.uuid4().hex[:8]
        with (root / 'server.log').open('w+') as log:
            server = subprocess.Popen([binary, 'server', endpoint, str(ready)],
                                      stdout=log, stderr=subprocess.STDOUT)
            code = 1
            try:
                deadline = time.monotonic() + 5
                while not ready.exists() and server.poll() is None and time.monotonic() < deadline:
                    time.sleep(.02)
                if not ready.exists():
                    raise RuntimeError('server did not signal readiness')
                for mode in ('client', 'rejected-client', 'client'):
                    client = subprocess.run([binary, mode, endpoint, str(ready)], timeout=10)
                    code = client.returncode
                    print(mode + '_exit', code, flush=True)
                    if code:
                        break
                server.wait(timeout=17)
                print('server_exit', server.returncode, flush=True)
                if server.returncode:
                    code = server.returncode
            finally:
                if server.poll() is None:
                    server.terminate()
                    try:
                        server.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        server.kill()
                        server.wait()
                log.seek(0)
                print(log.read(), flush=True)
        return code


if __name__ == '__main__':
    raise SystemExit(main())
