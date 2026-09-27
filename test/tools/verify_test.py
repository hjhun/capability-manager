# SPDX-License-Identifier: Apache-2.0
"""Exercise real verification CLI failures and explicitly selected fake transport."""
import json
import argparse
import ctypes
import importlib.util
import io
import os
import shlex
import tarfile
from unittest import mock
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[2] / 'tools' / 'verify.py'


class VerificationToolTest(unittest.TestCase):
    def test_missing_ctest_tree_cannot_report_pass(self):
        with tempfile.TemporaryDirectory(prefix='capmgr-tool-test-', dir=Path.cwd()) as directory:
            root = Path(directory)
            result = subprocess.run([sys.executable, str(SCRIPT), 'foundation',
                '--target', 'host', '--build-dir', str(root / 'missing'),
                '--output', str(root / 'evidence')], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            report = json.loads((root / 'evidence/summary.json').read_text())
            self.assertEqual(report['status'], 'FAIL')
            self.assertNotEqual(report['steps'][0]['exit'], 0)

    def test_transport_success_does_not_mask_remote_failure(self):
        with tempfile.TemporaryDirectory(prefix='capmgr-tool-test-', dir=Path.cwd()) as directory:
            root = Path(directory)
            sdb = root / 'fake-sdb'
            sdb.write_text('''#!/usr/bin/env python3
import subprocess,sys
if sys.argv[1:] == ['devices']:
 print('serial-fixture device fixture')
 sys.exit(0)
assert sys.argv[1:4] == ['-s', 'serial-fixture', 'shell']
subprocess.run(sys.argv[4], shell=True)
sys.exit(0)
''')
            sdb.chmod(0o755)
            result = subprocess.run([sys.executable, str(SCRIPT), 'smoke', '--target',
                'sdb', '--serial', 'serial-fixture', '--sdb', str(sdb),
                '--remote-bin-dir', str(root / 'no-installed-binaries'),
                '--output', str(root / 'evidence')], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            report = json.loads((root / 'evidence/summary.json').read_text())
            steps = [step for step in report['steps'] if 'transport_exit' in step]
            self.assertTrue(steps)
            self.assertTrue(all(step['transport_exit'] == 0 for step in steps))
            self.assertTrue(all(step['exit'] != 0 for step in steps))

    def test_sdb_requires_explicit_target(self):
        result = subprocess.run([sys.executable, str(SCRIPT), 'smoke', '--target',
            'sdb', '--output', '/unused-capmgr-test-output'], capture_output=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn(b'--serial is required', result.stderr)

    def test_watchdog_preserves_failure_and_enforces_deadline(self):
        watchdog = SCRIPT.with_name('run_bounded.py')
        for command, expected in [(['-c', 'raise SystemExit(7)'], 7),
                                   (['-c', 'import time; time.sleep(10)'], 124)]:
            result = subprocess.run([sys.executable, str(watchdog), '--seconds',
                '0.1', '--', sys.executable, *command], capture_output=True, timeout=3)
            self.assertEqual(result.returncode, expected)

    def test_existing_evidence_is_not_overwritten(self):
        with tempfile.TemporaryDirectory(prefix='capmgr-tool-test-', dir=Path.cwd()) as directory:
            evidence = Path(directory) / 'summary.json'
            evidence.write_text('preserve this')
            result = subprocess.run([sys.executable, str(SCRIPT), 'foundation',
                '--target', 'host', '--output', directory], capture_output=True)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(evidence.read_text(), 'preserve this')

    def test_watchdog_cleans_descendants_after_normal_exit_and_timeout(self):
        watchdog = SCRIPT.with_name('run_bounded.py')
        libc = ctypes.CDLL(None, use_errno=True)
        self.assertEqual(libc.prctl(36, 1, 0, 0, 0), 0)  # Linux child subreaper
        try:
            for tail, expected in [('exit 0', 0), ('wait', 124)]:
                with tempfile.TemporaryDirectory(prefix='capmgr-watchdog-test-') as directory:
                    pidfile = Path(directory) / 'child.pid'
                    command = 'sleep 600 & echo $! > ' + shlex.quote(str(pidfile)) + '; ' + tail
                    result = subprocess.run([sys.executable, str(watchdog), '--seconds',
                        '0.2', '--', 'sh', '-c', command], capture_output=True, timeout=3)
                    self.assertEqual(result.returncode, expected)
                    child = int(pidfile.read_text())
                    try:
                        reaped, status = os.waitpid(child, 0)
                    except ChildProcessError:
                        # The intermediate shell may have reaped its child before
                        # exiting. In that case require the process to be gone.
                        self.assertFalse(Path('/proc/' + str(child)).exists())
                    else:
                        self.assertEqual(reaped, child)
                        self.assertTrue(os.WIFSIGNALED(status))
        finally:
            libc.prctl(36, 0, 0, 0, 0)

    def test_native_scope_survives_unknown_remote_completion(self):
        spec = importlib.util.spec_from_file_location('capmgr_verify_test', SCRIPT)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        for transport_code in (23, 0):
            with tempfile.TemporaryDirectory(prefix='capmgr-transport-test-', dir=Path.cwd()) as directory:
                root = Path(directory)
                for filename, prefix in [('source.tgz', 'capability-manager-0.1.0'),
                                         ('json.tgz', 'json-3.11.3')]:
                    with tarfile.open(root / filename, 'w:gz') as archive:
                        member = tarfile.TarInfo(prefix + '/fixture');member.size=1
                        archive.addfile(member, io.BytesIO(b'x'))
                fake = root / 'sdb'
                fake.write_text("#!/usr/bin/env python3\nimport re,sys\n"
                    "if sys.argv[1:] == ['devices']:\n print('fixture device target');sys.exit(0)\n"
                    "command=sys.argv[-1]\n"
                    "if '--seconds 1700' in command: sys.exit(" + str(transport_code) + ")\n"
                    "marker=re.search(r'CAPMGR_EXIT_[a-f0-9]+=',command)\n"
                    "if marker: print(marker[0]+'0')\n")
                fake.chmod(0o755)
                args = argparse.Namespace(output=root/'evidence', sdb=str(fake), serial='fixture',
                    target='sdb', source_archive=root/'source.tgz', json_source=root/'json.tgz',
                    cleanup=True)
                runner = module.Runner(args)
                with mock.patch.object(module.hashlib, 'sha256') as digest:
                    digest.return_value.hexdigest.return_value = '0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406'
                    runner.native_build()
                self.assertNotIn('cleanup', [step['name'] for step in runner.steps])
                step = runner.steps[-1]
                self.assertEqual(step['name'], 'native-build')
                self.assertFalse(step['remote_complete'])
                self.assertNotEqual(step['exit'], 0)


if __name__ == '__main__':
    unittest.main()
