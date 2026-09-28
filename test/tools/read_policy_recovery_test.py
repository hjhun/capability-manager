# SPDX-License-Identifier: Apache-2.0
"""Unprivileged recovery mechanics only; no SMACK write or target operation."""
import contextlib
import errno
import fcntl
import io
import json
import os
from pathlib import Path
import select
import signal
import stat
import tempfile
import unittest
from unittest.mock import patch

import importlib.util
import subprocess

source = Path(__file__).parents[1]/'integration/read_policy_recovery.py'
spec = importlib.util.spec_from_file_location('read_policy_recovery', source)
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class FakeOperations(probe.Operations):
    owner = os.getuid()
    group = os.getgid()

    def __init__(self, parent):
        self.parent = Path(parent)
        self.writes = []
        self.denied = set()
        self.delete_error = False

    def trusted_parent(self):
        info = self.parent.lstat()
        probe.exact(info, self.owner, 0o700, directory=True)
        probe.no_acl(self.parent)

    def write_rule(self, subject, target, access):
        self.writes.append([subject, target, access])
        if (subject, target) in self.denied:
            raise OSError(errno.EACCES, 'injected revoke failure')

    def delete_scope(self, scope):
        if self.delete_error:
            raise OSError(errno.EIO, 'injected delete failure')
        super().delete_scope(scope)


class Fixture(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.parent = Path(self.temp.name)
        self.ops = FakeOperations(self.parent)
        self.scope = self.parent/(probe.PREFIX+'a'*32)
        self.scope.mkdir(mode=0o755)
        self.owner = probe.JournalOwner('a'*32, 'HostFixtureWriter',
                                        self.scope, self.ops)

    def tearDown(self):
        self.owner.close()
        self.temp.cleanup()

    def recover(self):
        self.owner.close()
        return probe.recover(self.owner.path, self.ops)

    def test_durable_plan_precedes_rules_and_command(self):
        plan = json.loads((self.owner.path/'plan.json').read_text())
        self.assertEqual(plan['rules'], probe.rule_matrix('a'*32, 'HostFixtureWriter'))
        self.assertEqual((self.owner.path/'plan.json').stat().st_mode & 0o7777, 0o600)
        self.assertEqual(self.ops.writes, [])
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.owner.install_rules(['/usr/bin/python3', '/trusted/probe.py',
                                      '--recover', str(self.owner.path)])
        self.assertTrue(output.getvalue().startswith('RECOVERY_ARGV='))
        self.assertEqual(self.ops.writes, plan['rules'])

    def test_recovery_refuses_live_owner_without_any_rule_or_delete(self):
        with self.assertRaisesRegex(RuntimeError, 'lock contended'):
            probe.recover(self.owner.path, self.ops)
        self.assertEqual(self.ops.writes, [])
        self.assertTrue(self.scope.exists())
        self.assertEqual(self.recover()['cleanup'], 'PASS')
        self.assertFalse(self.scope.exists())
        self.assertEqual(probe.recover(self.owner.path, self.ops)['cleanup'], 'PASS')

    def test_live_assertion_is_specific_and_never_mutates(self):
        before = (self.owner.path/'plan.json').read_bytes()
        result = probe.recover(self.owner.path, self.ops, assert_contention_only=True)
        self.assertEqual(result, {'assertion':'EX_CONTENDED', 'mutations':0})
        self.assertEqual(self.ops.writes, [])
        self.assertEqual((self.owner.path/'plan.json').read_bytes(), before)
        self.assertTrue(self.scope.is_dir())
        self.assertFalse((self.owner.path/'recovery.json').exists())

    def test_unexpected_ex_does_not_delete_revoke_or_write_receipt(self):
        self.owner.close()
        result = probe.recover(self.owner.path, self.ops, assert_contention_only=True)
        self.assertEqual(result, {'assertion':'UNEXPECTED_EX_AVAILABLE', 'mutations':0})
        self.assertEqual(self.ops.writes, [])
        self.assertTrue(self.scope.exists())
        self.assertFalse((self.owner.path/'recovery.json').exists())
        self.assertEqual(probe.recover(self.owner.path, self.ops)['cleanup'], 'PASS')
        self.assertEqual(len(self.ops.writes), 9)

    def test_independent_ex_holder_is_contended_without_identity_inference(self):
        self.owner.close()
        independent = os.open(self.owner.path/'lock', os.O_RDWR | os.O_CLOEXEC)
        try:
            fcntl.flock(independent, fcntl.LOCK_EX | fcntl.LOCK_NB)
            self.assertEqual(probe.recover(self.owner.path, self.ops,
                                           assert_contention_only=True),
                             {'assertion':'EX_CONTENDED', 'mutations':0})
            self.assertEqual(self.ops.writes, [])
            self.assertTrue(self.scope.exists())
        finally:
            os.close(independent)

    def test_both_assertion_outcomes_preserve_existing_receipts(self):
        receipts = {'recovery.json':b'previous-receipt\0bytes',
                    'recovery.json.next':b'stale-receipt\0bytes'}
        for name, data in receipts.items():
            path = self.owner.path/name
            path.write_bytes(data)
            path.chmod(0o600)
        for expected in ('EX_CONTENDED', 'UNEXPECTED_EX_AVAILABLE'):
            with patch.object(probe, 'persist') as persisted, \
                 patch.object(self.ops, 'delete_scope') as deleted:
                result = probe.recover(self.owner.path, self.ops,
                                       assert_contention_only=True)
                self.assertEqual(result, {'assertion':expected, 'mutations':0})
                persisted.assert_not_called()
                deleted.assert_not_called()
            self.assertEqual(self.ops.writes, [])
            for name, data in receipts.items():
                self.assertEqual((self.owner.path/name).read_bytes(), data)
            self.owner.close()

    def test_assertion_does_not_mask_corrupt_plan_or_wrong_scope(self):
        plan = self.owner.path/'plan.json'
        original = plan.read_bytes()
        plan.write_bytes(b'{}')
        with self.assertRaises(RuntimeError):
            probe.recover(self.owner.path, self.ops, assert_contention_only=True)
        self.assertEqual(self.ops.writes, [])
        value = json.loads(original)
        value['scope_identity'][1] += 1000
        plan.write_text(json.dumps(value))
        with self.assertRaisesRegex(RuntimeError, 'scope changed'):
            probe.recover(self.owner.path, self.ops, assert_contention_only=True)
        self.assertEqual(self.ops.writes, [])

    def test_child_inherited_reference_survives_parent_close(self):
        # No SQLite exists anywhere in this test. Exclusive direct-child cleanup
        # is bounded and never used by the recovery implementation.
        ready_r, ready_w = os.pipe()
        release_r, release_w = os.pipe()
        child = os.fork()
        if child == 0:
            try:
                os.close(ready_r)
                os.close(release_w)
                os.close(self.owner.directory_fd)
                os.write(ready_w, b'R')
                if not select.select([release_r], [], [], 5)[0]:
                    os._exit(2)
                os.read(release_r, 1)
                os._exit(0)
            except BaseException:
                os._exit(3)
        try:
            os.close(ready_w)
            os.close(release_r)
            self.assertTrue(select.select([ready_r], [], [], 3)[0])
            self.assertEqual(os.read(ready_r, 1), b'R')
            self.owner.close()
            with self.assertRaisesRegex(RuntimeError, 'lock contended'):
                probe.recover(self.owner.path, self.ops)
            self.assertEqual(probe.recover(self.owner.path, self.ops,
                                          assert_contention_only=True),
                             {'assertion':'EX_CONTENDED', 'mutations':0})
            self.assertEqual(self.ops.writes, [])
            os.write(release_w, b'X')
            # waitpid is only after a bounded child-ready release. Poll to retain
            # exclusive ownership; failure cleanup kills then reaps that child.
            import time
            end = time.monotonic()+3
            status = None
            while time.monotonic() < end:
                pid, value = os.waitpid(child, os.WNOHANG)
                if pid:
                    status = value
                    child = 0
                    break
                time.sleep(.01)
            self.assertIsNotNone(status)
            self.assertEqual(os.waitstatus_to_exitcode(status), 0)
            self.assertEqual(probe.recover(self.owner.path, self.ops)['cleanup'], 'PASS')
        finally:
            if child:
                os.kill(child, signal.SIGKILL)
                os.waitpid(child, 0)
            os.close(ready_r)
            os.close(release_w)

    def test_best_effort_delete_and_revoke_then_repeat(self):
        self.ops.delete_error = True
        self.ops.denied.add(tuple(self.owner.rules[0][:2]))
        result = self.recover()
        self.assertEqual(len(self.ops.writes), 9)
        self.assertEqual(result['cleanup'], 'BLOCKED')
        self.assertEqual(result['remaining_rules'], [self.owner.rules[0][:2]])
        self.assertTrue(self.scope.exists())
        self.ops.delete_error = False
        self.ops.denied.clear()
        self.assertEqual(probe.recover(self.owner.path, self.ops)['cleanup'], 'PASS')
        self.assertFalse(self.scope.exists())
        self.assertEqual(len(self.ops.writes), 18)

    def test_partial_install_always_retains_complete_plan(self):
        self.ops.denied.add(tuple(self.owner.rules[2][:2]))
        with contextlib.redirect_stdout(io.StringIO()), self.assertRaises(OSError):
            self.owner.install_rules(['/usr/bin/python3', '/trusted/probe.py',
                                      '--recover', str(self.owner.path)])
        self.assertEqual(len(self.ops.writes), 3)
        self.ops.denied.clear()
        self.ops.writes.clear()
        self.assertEqual(self.recover()['cleanup'], 'PASS')
        self.assertEqual(len(self.ops.writes), 9)

    def test_changed_scope_identity_not_deleted_but_all_rules_revoked(self):
        original = self.scope.with_name('held-original')
        self.scope.rename(original)
        self.scope.mkdir(mode=0o755)
        result = self.recover()
        self.assertEqual(result['cleanup'], 'BLOCKED')
        self.assertEqual(len(self.ops.writes), 9)
        self.assertTrue(self.scope.exists())
        self.assertTrue(original.exists())

    def test_unsafe_plan_symlink_and_duplicate_keys_fail_before_mutation(self):
        self.owner.close()
        plan = self.owner.path/'plan.json'
        plan.write_text('{"version":1,"version":1}')
        with self.assertRaisesRegex(RuntimeError, 'duplicate'):
            probe.recover(self.owner.path, self.ops)
        plan.unlink()
        plan.symlink_to('/etc/passwd')
        with self.assertRaises(OSError):
            probe.recover(self.owner.path, self.ops)
        self.assertEqual(self.ops.writes, [])
        self.assertTrue(self.scope.exists())

    def test_acl_error_is_not_absence(self):
        with patch.object(probe.os, 'getxattr', side_effect=OSError(errno.EACCES, 'denied')):
            with self.assertRaisesRegex(RuntimeError, 'ACL lookup failed'):
                probe.recover(self.owner.path, self.ops)
        self.assertEqual(self.ops.writes, [])

    def test_unsafe_ancestor_refused_before_open(self):
        self.parent.chmod(0o777)
        try:
            with patch.object(probe.os, 'open') as opened:
                with self.assertRaisesRegex(RuntimeError, 'unsafe journal object'):
                    probe.recover(self.owner.path, self.ops)
                opened.assert_not_called()
        finally:
            self.parent.chmod(0o700)
        self.assertEqual(self.ops.writes, [])

    def test_failed_receipt_fsync_retains_plan_then_exact_retry(self):
        self.owner.close()
        with patch.object(probe.os, 'fsync', side_effect=OSError(errno.EIO, 'fsync')):
            with self.assertRaises(OSError):
                probe.recover(self.owner.path, self.ops)
        self.assertTrue((self.owner.path/'plan.json').exists())
        self.assertTrue((self.owner.path/'recovery.json.next').exists())
        self.assertEqual(probe.recover(self.owner.path, self.ops)['cleanup'], 'PASS')
        self.assertFalse((self.owner.path/'recovery.json.next').exists())
        self.assertEqual(len(self.ops.writes), 18)

    def test_no_rules_before_failed_plan_persist(self):
        scope = self.parent/(probe.PREFIX+'b'*32)
        scope.mkdir(mode=0o755)
        with patch.object(probe.os, 'fsync', side_effect=OSError(errno.EIO, 'fsync')):
            with self.assertRaises(OSError):
                probe.JournalOwner('b'*32, 'HostFixtureWriter', scope, self.ops)
        self.assertEqual(self.ops.writes, [])
        journal = self.parent/(probe.JOURNAL_PREFIX+'b'*32)
        self.assertFalse((journal/'plan.json').exists())
        with self.assertRaises(FileNotFoundError):
            probe.recover(journal, self.ops)
        self.assertTrue(scope.exists())

    def test_unsafe_rmtree_refuses_but_revocations_continue(self):
        with patch.object(probe.shutil, 'rmtree') as remove:
            remove.avoids_symlink_attacks = False
            result = self.recover()
            remove.assert_not_called()
        self.assertEqual(result['cleanup'], 'BLOCKED')
        self.assertEqual(len(self.ops.writes), 9)
        self.assertTrue(self.scope.exists())


class CliTests(unittest.TestCase):
    def test_no_default_policy_run(self):
        result = subprocess.run([os.sys.executable, str(source)],
                                capture_output=True, text=True, timeout=3)
        self.assertEqual(result.returncode, 2)
        self.assertIn('--recover', result.stderr)

    def test_cli_provenance_refuses_before_recovery(self):
        with patch.object(probe.os, 'geteuid', return_value=0), \
             patch.object(probe, 'trusted_source', side_effect=RuntimeError('unsafe source')), \
             patch.object(probe, 'recover') as recover:
            with self.assertRaisesRegex(RuntimeError, 'unsafe source'):
                probe.main(['--recover', '/opt/usr/capmgr-read-policy-recovery-'+'a'*32])
            recover.assert_not_called()

    def test_live_assertion_cli_guard_and_specific_result(self):
        path = '/opt/usr/capmgr-read-policy-recovery-'+'a'*32
        with patch.object(probe.os, 'geteuid', return_value=0), \
             patch.object(probe, 'trusted_source', side_effect=RuntimeError('unsafe source')), \
             patch.object(probe, 'recover') as recover:
            with self.assertRaisesRegex(RuntimeError, 'unsafe source'):
                probe.main(['--assert-contended', path])
            recover.assert_not_called()
        for result, expected in [({'assertion':'EX_CONTENDED', 'mutations':0}, 0),
                                 ({'assertion':'UNEXPECTED_EX_AVAILABLE', 'mutations':0}, 1)]:
            with patch.object(probe.os, 'geteuid', return_value=0), \
                 patch.object(probe, 'trusted_source'), \
                 patch.object(probe, 'recover', return_value=result) as recover, \
                 contextlib.redirect_stdout(io.StringIO()) as output:
                self.assertEqual(probe.main(['--assert-contended', path]), expected)
                recover.assert_called_once_with(Path(path), assert_contention_only=True)
                self.assertEqual(output.getvalue(),
                    'READ_POLICY_LOCK_ASSERTION='+json.dumps(result, sort_keys=True)+'\n')

    def test_dependency_source_checked_before_import(self):
        with patch.object(probe.os, 'geteuid', return_value=0), \
             patch.object(probe, 'trusted_source', side_effect=RuntimeError('unsafe dependency')):
            with self.assertRaisesRegex(RuntimeError, 'unsafe dependency'):
                probe.Operations().trusted_parent()

    def test_valid_conflicting_bytecode_cannot_replace_source(self):
        import importlib.machinery
        import importlib._bootstrap_external as external
        import types
        with tempfile.TemporaryDirectory() as directory, \
             patch.object(os.sys, 'pycache_prefix', None):
            dependency = Path(directory)/'db_access_probe.py'
            dependency.write_text("ORIGIN = 'validated-source'\ndef trusted_parent(): return ORIGIN\n")
            dependency.chmod(0o600)
            info = dependency.stat()
            cache = Path(importlib.util.cache_from_source(str(dependency)))
            cache.parent.mkdir(parents=True, exist_ok=True)
            cached_code = compile("ORIGIN = 'conflicting-cache'\ndef trusted_parent(): return ORIGIN\n",
                                  str(dependency), 'exec')
            cache.write_bytes(external._code_to_timestamp_pyc(
                cached_code, int(info.st_mtime), info.st_size))
            # Prove this really is a valid competing cache for the actual
            # interpreter, not merely an invalid pyc that any loader rejects.
            cached = types.ModuleType('fixture_cached')
            importlib.machinery.SourceFileLoader('fixture_cached', str(dependency)).exec_module(cached)
            self.assertEqual(cached.trusted_parent(), 'conflicting-cache')
            original_fstat = os.fstat
            class RootInfo:
                st_uid = 0
                def __init__(self, value): self.value = value
                def __getattr__(self, name): return getattr(self.value, name)
            with patch.object(probe, 'trusted_source') as checked, \
                 patch.object(probe.os, 'fstat', side_effect=lambda fd: RootInfo(original_fstat(fd))):
                actual = probe.source_module(dependency)
                checked.assert_called_once_with(dependency)
            self.assertEqual(actual.trusted_parent(), 'validated-source')
            self.assertEqual(actual.__file__, str(dependency))
            self.assertEqual(actual.__name__, 'capmgr_read_policy_trusted_parent')

    def test_actual_source_guard_rejects_writable_ancestor_and_leaf(self):
        import types
        source = Path('/safe/db_access_probe.py')
        def info(path, bad):
            return types.SimpleNamespace(st_uid=0, st_nlink=1,
                st_mode=(stat.S_IFREG | 0o644 if path == source else stat.S_IFDIR | 0o755)
                        | (0o022 if path == bad else 0))
        for bad in (Path('/safe'), source):
            with patch.object(Path, 'lstat', lambda path: info(path, bad)), \
                 patch.object(probe.os, 'getxattr', side_effect=OSError(errno.ENODATA, 'absent')):
                with self.assertRaisesRegex(RuntimeError, 'unsafe source ancestor' if bad != source else 'unsafe recovery source'):
                    probe.trusted_source(source)

    def test_actual_source_guard_rejects_acl_before_loading(self):
        import types
        source = Path('/safe/db_access_probe.py')
        def info(path):
            return types.SimpleNamespace(st_uid=0, st_nlink=1,
                st_mode=stat.S_IFREG | 0o644 if path == source else stat.S_IFDIR | 0o755)
        with patch.object(Path, 'lstat', info), \
             patch.object(probe.os, 'getxattr', return_value=b'ACL'), \
             patch.object(probe.os, 'open') as opened:
            with self.assertRaisesRegex(RuntimeError, 'ACL present'):
                probe.source_module(source)
            opened.assert_not_called()

    def test_acl_and_permission_error_are_not_safe_absence(self):
        with patch.object(probe.os, 'getxattr', return_value=b'ACL'):
            with self.assertRaisesRegex(RuntimeError, 'ACL present'):
                probe.no_acl(source)
        with patch.object(probe.os, 'getxattr', side_effect=OSError(errno.EACCES, 'no metadata')):
            with self.assertRaisesRegex(RuntimeError, 'ACL lookup failed'):
                probe.no_acl(source)


if __name__ == '__main__':
    unittest.main()
