# SPDX-License-Identifier: Apache-2.0
"""Unprivileged tests of the root fixture's abrupt-death recovery mechanics."""
import fcntl
import importlib.util
import json
import io
import contextlib
import os
from pathlib import Path
import signal
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('db_access_probe', Path(__file__).parents[1]/'integration/db_access_probe.py')
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class RecoveryTests(unittest.TestCase):
    def test_journal_durable_before_policy(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'plan.json'
            plan = {'scope': 'fixture', 'rules': [['new', 'label', 'r']]}
            probe.persist(path, plan)
            self.assertEqual(json.loads(path.read_text()), plan)
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)
            self.assertFalse(path.with_suffix('.new').exists())

    def test_inherited_lock_blocks_recovery_after_parent_reference_closes(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'lock'
            fd = os.open(path, os.O_CREAT | os.O_RDWR, 0o600)
            fcntl.flock(fd, fcntl.LOCK_SH)
            ready_r, ready_w = os.pipe()
            child = os.fork()
            if child == 0:
                os.close(ready_r)
                os.write(ready_w, b'R')
                while True:
                    signal.pause()
            try:
                os.close(ready_w)
                self.assertEqual(os.read(ready_r, 1), b'R')
                os.close(fd)
                fd = -1
                recovery = os.open(path, os.O_RDWR)
                try:
                    with self.assertRaises(BlockingIOError):
                        fcntl.flock(recovery, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    os.kill(child, signal.SIGKILL)
                    os.waitpid(child, 0)
                    child = 0
                    fcntl.flock(recovery, fcntl.LOCK_EX | fcntl.LOCK_NB)
                finally:
                    os.close(recovery)
            finally:
                os.close(ready_r)
                if fd >= 0:
                    os.close(fd)
                if child:
                    os.kill(child, signal.SIGKILL)
                    os.waitpid(child, 0)

    def test_actual_recover_refuses_live_role_then_retries(self):
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory)
            key = 'a'*32
            journal = parent/('capmgr-db-recovery-'+key)
            journal.mkdir(mode=0o700)
            root = parent/('capmgr-db-access-'+key)
            root.mkdir()
            info = root.stat()
            plan = {'journal': str(journal), 'scope': str(root),
                    'scope_identity': [info.st_dev, info.st_ino], 'rules': [['new', 'DB', 'r']]}
            probe.persist(journal/'plan.json', plan)
            fd = os.open(journal/'lock', os.O_CREAT | os.O_RDWR, 0o600)
            fcntl.flock(fd, fcntl.LOCK_SH)
            # Host user ownership is substituted; the real flock, plan parsing,
            # scope identity, deletion and recovery receipt paths execute.
            original_lstat, original_fstat = Path.lstat, os.fstat
            def owned(info):
                values = list(info)
                values[4] = 0
                return os.stat_result(values)
            try:
                with patch.object(probe, 'RECOVERY_PARENT', parent), \
                     patch.object(probe, 'trusted_parent'), \
                     patch.object(Path, 'lstat', lambda path: owned(original_lstat(path))), \
                     patch.object(os, 'fstat', lambda number: owned(original_fstat(number))), \
                     patch.object(probe, 'rule') as revoke, contextlib.redirect_stdout(io.StringIO()):
                    with self.assertRaisesRegex(RuntimeError, 'still holds lock'):
                        probe.recover(journal)
                    revoke.assert_not_called()
                    os.close(fd)
                    fd = -1
                    self.assertEqual(probe.recover(journal), 0)
                    self.assertFalse(root.exists())
                    revoke.assert_called_once_with('new', 'DB', '------')
                    self.assertEqual(json.loads((journal/'recovery.json').read_text())['cleanup'], 'PASS')
            finally:
                if fd >= 0:
                    os.close(fd)

    def test_untrusted_parent_refused_before_recovery_open(self):
        with patch.object(probe, 'trusted_parent', side_effect=RuntimeError('unsafe recovery ancestor')), \
             patch.object(os, 'open') as opened:
            with self.assertRaisesRegex(RuntimeError, 'unsafe recovery ancestor'):
                probe.recover(Path('/opt/usr/capmgr-db-recovery-'+'a'*32))
            opened.assert_not_called()

    def test_writable_parent_and_acl_rejected(self):
        real_stat = os.stat('/proc/self/ns/mnt')
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory)
            parent.chmod(0o777)
            def safe_info(mode=0o755):
                return os.stat_result((probe.stat.S_IFDIR | mode, 1, 1, 1, 0, 0, 0, 0, 0, 0))
            with patch.object(probe, 'RECOVERY_PARENT', parent), \
                 patch.object(Path, 'lstat', lambda path: safe_info(0o777 if path == parent else 0o755)), \
                 patch.object(os, 'stat', return_value=real_stat), \
                 patch.object(os, 'getxattr', side_effect=OSError(probe.errno.ENODATA, 'absent')):
                with self.assertRaisesRegex(RuntimeError, 'unsafe recovery ancestor'):
                    probe.trusted_parent()
            with patch.object(os, 'stat', return_value=real_stat), \
                 patch.object(Path, 'lstat', return_value=safe_info()), \
                 patch.object(os, 'getxattr', return_value=b'ACL'):
                with self.assertRaisesRegex(RuntimeError, 'ancestor ACL'):
                    probe.trusted_parent()

    def test_cleanup_failure_does_not_skip_later_revocations(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)/'scope'
            root.mkdir()
            info = root.stat()
            plan = {'scope': str(root), 'scope_identity': [info.st_dev, info.st_ino],
                    'rules': [['a', 'new', 'r'], ['b', 'new', 'r']]}
            with patch.object(probe.shutil, 'rmtree', side_effect=OSError('delete denied')) as removal, \
                 patch.object(probe, 'rule', side_effect=[OSError('first revoke'), None]) as revoke:
                removal.avoids_symlink_attacks = True
                result = probe.cleanup_scope(plan)
            self.assertEqual(revoke.call_count, 2)
            self.assertEqual(result['remaining_rules'], [['a', 'new']])
            self.assertEqual(result['cleanup'], 'BLOCKED')
            self.assertEqual(len(result['cleanup_errors']), 2)

    def test_unsafe_cleanup_refused_but_rules_revoked(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)/'scope'
            root.mkdir()
            info = root.stat()
            plan = {'scope': str(root), 'scope_identity': [info.st_dev, info.st_ino], 'rules': [['a', 'new', 'r']]}
            with patch.object(probe.shutil, 'rmtree') as removal, patch.object(probe, 'rule') as revoke:
                removal.avoids_symlink_attacks = False
                result = probe.cleanup_scope(plan)
                removal.assert_not_called()
                revoke.assert_called_once_with('a', 'new', '------')
            self.assertTrue(root.exists())
            self.assertEqual(result['cleanup'], 'BLOCKED')


if __name__ == '__main__':
    unittest.main()
