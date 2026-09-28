# SPDX-License-Identifier: Apache-2.0
"""Opt-in fixed root writer reference hold only, no policy/SQLite/context drop.

Ordinary CTest never selects CAPMGR_READ_POLICY_HOLD_IMAGE. Native execution needs
separate exact-source/image/protected-path safety review. Fake roles are not used
as evidence for this timer/lifetime contract.
"""
import fcntl
import importlib.util
import os
from pathlib import Path
import tempfile
import time
import unittest
import uuid

source = Path(__file__).parents[1]/'integration/read_policy_roles.py'
spec = importlib.util.spec_from_file_location('read_policy_roles_hold', source)
roles = importlib.util.module_from_spec(spec)
spec.loader.exec_module(roles)


class FixedWriterHold(unittest.TestCase):
    def setUp(self):
        image = os.environ.get('CAPMGR_READ_POLICY_HOLD_IMAGE')
        if not image:
            self.skipTest('fixed hold image not selected; no root workload')
        if os.getuid() != 0 or os.getgid() != 0:
            self.skipTest('root-owned reference required; no credential bypass')
        self.image = Path(image)
        self.temp = tempfile.TemporaryDirectory(prefix='hold-test-', dir=Path.cwd())
        self.root = Path(self.temp.name)
        self.lock_path = self.root/'reference'
        self.lock = os.open(self.lock_path,
                            os.O_RDWR | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC, 0o600)
        os.fchmod(self.lock, 0o600)
        fcntl.flock(self.lock, fcntl.LOCK_SH)
        self.role = roles.Role()  # retained before positive spawn
        self.key = uuid.uuid4().hex
        self.catalog = Path('/opt/usr/capmgr-read-policy-'+self.key)
        self.assertFalse(os.path.lexists(self.catalog))

    def tearDown(self):
        # stop() is fallback only after failed assertions, never the positive
        # hold proof. Unknown ownership raises before deleting any owned scope.
        try:
            self.role.stop()
        except BaseException:
            self.temp._finalizer.detach()
            print('RETAINED_HOLD_TEST_SCOPE='+str(self.root), flush=True)
            raise
        if self.lock >= 0:
            os.close(self.lock)
        self.assertFalse(os.path.lexists(self.catalog))
        self.temp.cleanup()

    def start(self):
        self.role.spawn(self.image, self.lock, {'role':'writer', 'key':self.key})
        self.assertEqual(self.role.context['stage'], 'writer-context')

    def test_hold_survives_parent_references_close_then_owned_normal_exit(self):
        self.start()
        start = time.monotonic()
        reply = self.role.request('hold-reference')
        self.assertEqual(reply, {'stage':'hold-reference', 'seconds':20,
                                 'authority':'reference lifetime only'})
        os.close(self.role.command)
        self.role.command = -1
        os.close(self.lock)
        self.lock = -1
        independent = os.open(self.lock_path, os.O_RDWR | os.O_CLOEXEC)
        try:
            # The actual owned wait, separately from lock contention, establishes
            # our unreaped direct child has not exited at this observation.
            self.assertIsNone(self.role.wait(.03))
            with self.assertRaises(BlockingIOError):
                fcntl.flock(independent, fcntl.LOCK_EX | fcntl.LOCK_NB)
            self.assertEqual(self.role.wait(25), 0)
            self.assertLess(time.monotonic()-start, 28)
            fcntl.flock(independent, fcntl.LOCK_EX | fcntl.LOCK_NB)
        finally:
            os.close(independent)
        self.role.stop()  # no signal after actual normal wait/reap proof
        self.assertFalse(os.path.lexists(self.catalog))

    def test_malformed_hold_rejected_before_ack_or_wait(self):
        self.start()
        start = time.monotonic()
        roles.send(self.role.command, {'command':'hold-reference', 'seconds':0})
        with self.assertRaisesRegex(RuntimeError, 'hold without writer only'):
            roles.message(self.role.reply)
        self.assertEqual(self.role.wait(5), 1)
        self.assertLess(time.monotonic()-start, 6)
        self.role.stop()
        os.close(self.lock)
        self.lock = -1
        independent = os.open(self.lock_path, os.O_RDWR | os.O_CLOEXEC)
        try:
            fcntl.flock(independent, fcntl.LOCK_EX | fcntl.LOCK_NB)
        finally:
            os.close(independent)


if __name__ == '__main__':
    unittest.main()
