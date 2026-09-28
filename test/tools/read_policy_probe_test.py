# SPDX-License-Identifier: Apache-2.0
"""Root fixture safety routing/fault tests, never real policy/roles/SQLite."""
import importlib.util
import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import Mock, patch

source = Path(__file__).parents[1]/'integration/read_policy_probe.py'
spec = importlib.util.spec_from_file_location('read_policy_probe_tests', source)
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class JournalCli(unittest.TestCase):
    def invoke(self, output, assertion=False, code=0):
        result = types.SimpleNamespace(stdout=output, returncode=code)
        with patch.object(probe, 'trusted_source'), \
                patch.object(probe, 'executable'), \
                patch.object(probe.subprocess, 'run', return_value=result) as run:
            probe.journal_cli(Path('/fixed-fixture/journal'), assertion)
        return run.call_args

    def test_specific_contention_with_clean_fixed_cli(self):
        call = self.invoke(b'READ_POLICY_LOCK_ASSERTION={"assertion":"EX_CONTENDED",'
                           b'"mutations":0}\n', assertion=True)
        args = call.args[0]
        self.assertEqual(args[-2:], ['--assert-contended','/fixed-fixture/journal'])
        self.assertEqual(args[1], '-B')
        self.assertEqual(call.kwargs['timeout'], 8)
        self.assertEqual(call.kwargs['env'],
                         {'PATH':'/usr/bin:/bin','LANG':'C',
                          'PYTHONNOUSERSITE':'1','PYTHONDONTWRITEBYTECODE':'1'})

    def test_unexpected_ex_is_not_contention_even_with_zero_exit(self):
        with self.assertRaisesRegex(RuntimeError, 'result mismatch'):
            self.invoke(b'READ_POLICY_LOCK_ASSERTION={"assertion":"UNEXPECTED_EX_AVAILABLE",'
                        b'"mutations":0}\n', True)

    def test_nonzero_provenance_failure_never_counts_as_refusal(self):
        with self.assertRaisesRegex(RuntimeError, 'CLI failed'):
            self.invoke(b'unsafe recovery source\n', True, 1)

    def test_duplicate_missing_malformed_and_oversize_results(self):
        row = b'READ_POLICY_LOCK_ASSERTION={"assertion":"EX_CONTENDED","mutations":0}\n'
        for data, cause in [(b'', 'missing/duplicate'),
                            (row+row, 'missing/duplicate'),
                            (b'x'*32769, 'output limit')]:
            with self.assertRaisesRegex(RuntimeError, cause):
                self.invoke(data, True)
        with self.assertRaisesRegex(RuntimeError, 'duplicate journal'):
            self.invoke(b'READ_POLICY_LOCK_ASSERTION={"assertion":"bad","assertion":"EX_CONTENDED","mutations":0}\n', True)
        with self.assertRaises(json.JSONDecodeError):
            self.invoke(b'READ_POLICY_LOCK_ASSERTION=bad\n', True)

    def test_normal_recovery_requires_empty_error_and_rule_receipt(self):
        self.invoke(b'READ_POLICY_RECOVERY={"cleanup":"PASS","errors":[],"remaining_rules":[]}\n')
        with self.assertRaisesRegex(RuntimeError, 'result mismatch'):
            self.invoke(b'READ_POLICY_RECOVERY={"cleanup":"PASS","errors":[],"remaining_rules":[["a","b"]]}\n')

    def test_timeout_propagates_without_synthesizing_cleanup(self):
        with patch.object(probe, 'trusted_source'), \
                patch.object(probe, 'executable'), \
                patch.object(probe.subprocess, 'run',
                             side_effect=subprocess.TimeoutExpired('fixed', 8)), \
                self.assertRaises(subprocess.TimeoutExpired):
            probe.journal_cli(Path('/fixed-fixture/journal'), True)


class Barrier(unittest.TestCase):
    def test_first_write_failure_cannot_ack_barrier_or_write_second_rule(self):
        self.coordinate(write_fails=True)

    def test_one_completed_write_precedes_ack_then_no_second_rule(self):
        self.coordinate(write_fails=False)

    def coordinate(self, write_fails):
        events = []
        class FakeRole:
            pid = 12345
            uncertain_spawn = False
            context = {'stage':'writer-context','label':'test-writer',
                       'scope':'root-writer-only'}
            def spawn(self, *args): events.append('spawn')
            def request(self, command):
                events.append(command)
                return {'stage':'hold-reference','seconds':20,
                        'authority':'reference lifetime only'}
            def stop(self): events.append('owned-stop')
        class FakeOperations:
            def write_rule(self, *args):
                events.append(('write', args))
                if write_fails: raise OSError('injected checked write failure')
                events.append('checked-write-close')
        class FakeOwner:
            ready = True
            lock_fd = 5  # fake transport receives it, never passed to real spawn
            rules = [('writer','new-object','rwx'), ('writer','second','rwx')]
            ops = FakeOperations()
            def __init__(self, key, writer, scope):
                self.path = scope.parent/('journal-'+key)
                events.append('durable-full-plan')
            def close(self): events.append('journal-close')
        def send(fd, value): events.append(('status', value['stage']))
        with tempfile.TemporaryDirectory() as temporary:
            recovery = types.SimpleNamespace(PARENT=Path(temporary), PREFIX='scope-',
                                              JournalOwner=FakeOwner)
            transport = types.SimpleNamespace(Role=FakeRole, task_label=lambda:'test-writer',
                                               send=send,
                                               message=lambda *args: (_ for _ in ()).throw(
                                                   RuntimeError('injected barrier loss')))
            with patch.object(probe, 'recovery', recovery, create=True), \
                    patch.object(probe, 'transport', transport, create=True):
                self.assertEqual(probe.crash_coordinator(3, 4, 'a'*32, Path('/fixed/image')), 1)
        writes = [e for e in events if isinstance(e, tuple) and e[0]=='write']
        self.assertEqual(writes, [('write', ('writer','new-object','rwx'))])
        self.assertLess(events.index('durable-full-plan'), events.index(writes[0]))
        self.assertLess(events.index('hold-reference'), events.index(writes[0]))
        if write_fails:
            self.assertNotIn(('status','first-write-complete'), events)
        else:
            self.assertLess(events.index('checked-write-close'),
                            events.index(('status','first-write-complete')))
        self.assertEqual(events[-2:], ['owned-stop','journal-close'])

    def test_receipt_snapshot_is_read_only_for_existing_and_absent_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root/'plan.json').write_bytes(b'plan')
            (root/'recovery.json.next').write_bytes(b'preserve-next')
            expected = {'plan.json':b'plan','recovery.json':None,
                        'recovery.json.next':b'preserve-next'}
            self.assertEqual(probe.receipt_snapshot(root), expected)
            self.assertEqual(probe.receipt_snapshot(root), expected)

    def test_import_and_no_default_flag_cannot_start_policy_fixture(self):
        with patch.object(probe, 'run_matrix') as matrix, \
                patch.object(probe, 'run_crash') as crash, \
                patch.object(sys, 'argv', ['fixed-probe']), \
                self.assertRaisesRegex(RuntimeError, 'explicit --run'):
            probe.main()
        matrix.assert_not_called()
        crash.assert_not_called()


class CrashOwnership(unittest.TestCase):
    """Exercise the actual orchestrator with no fork/FD/filesystem/policy IO."""
    def run_fixture(self, boundaries, child_fault=None, assertion_error=None,
                    setup_error=None):
        records = []
        class FakeChild:
            def __init__(self):
                self.pid = None
                self.status = None
                self.uncertain = False
                records.append(self)
            def attach_direct(self, pid):
                self.pid = pid
                self.uncertain = False
            def verify_adoption(self, pid):
                self.pid = pid
                if child_fault == 'adoption':
                    self.uncertain = True
                    raise ChildProcessError('injected lost adoption proof')
                self.uncertain = False
                return True
            def observe(self):
                if child_fault == 'observation':
                    self.uncertain = True
                    raise ChildProcessError('injected lost ownership proof')
                return False
            def kill_and_wait(self, seconds):
                # Deliberately simulate eventual known-child cleanup after an
                # earlier lost proof. It must not restore recovery eligibility.
                self.status = -probe.signal.SIGKILL
                self.uncertain = False
                return self.status
        key = 'a'*32
        parent = Path('/fixed-fixture')
        recovery = types.SimpleNamespace(PARENT=parent, JOURNAL_PREFIX='journal-',
                                         PREFIX='scope-')
        ready = {'stage':'journal-ready','key':key,
                 'journal':str(parent/('journal-'+key)),
                 'scope':str(parent/('scope-'+key))}
        transport = types.SimpleNamespace(message=Mock(side_effect=[
            ready, {'stage':'held','pid':200},
            {'stage':'first-write-complete','rows':1}]))
        children = types.SimpleNamespace(OwnedChild=FakeChild,
            prepare_wait_boundaries=Mock(side_effect=setup_error),
            require_no_children=Mock(side_effect=boundaries))
        output = io.StringIO()
        with contextlib.ExitStack() as stack:
            stack.enter_context(contextlib.redirect_stdout(output))
            for name, value in [('recovery', recovery), ('transport', transport),
                                ('children', children)]:
                stack.enter_context(patch.object(probe, name, value, create=True))
            for target, attribute, kwargs in [
                (probe, 'dependencies', {}), (probe, 'executable', {}),
                (probe, 'receipt_snapshot', {'return_value':{'plan.json':b'fixed'}}),
                (probe, 'journal_cli', {'side_effect':assertion_error}),
                (probe, 'real_recovery', {}),
                (probe.os, 'getresuid', {'return_value':(0,0,0)}),
                (probe.os, 'getresgid', {'return_value':(0,0,0)}),
                (probe.os, 'pipe2', {'side_effect':[(30,31),(32,33)]}),
                (probe.os, 'fork', {'return_value':100}),
                (probe.os, 'close', {}), (probe.os, 'set_blocking', {}),
                (probe.os.path, 'lexists', {'return_value':False}),
                (probe.signal, 'pthread_sigmask', {'return_value':set()}),
                (probe.uuid, 'uuid4', {'return_value':types.SimpleNamespace(hex=key)}),
                (Path, 'lstat', {'return_value':types.SimpleNamespace(st_dev=1,st_ino=2)})
            ]:
                stack.enter_context(patch.object(target, attribute, **kwargs))
            recovery_call = probe.real_recovery
            boundary_call = children.require_no_children
            fork_call = probe.os.fork
            error = None
            try:
                probe.run_crash()
            except BaseException as caught:
                error = caught
        return output.getvalue(), error, recovery_call, boundary_call, records, fork_call

    def test_unexpected_child_then_echild_at_each_empty_boundary(self):
        for boundary in range(3):
            with self.subTest(boundary=boundary):
                outcomes = [None]*boundary+[RuntimeError('child absence not proved'),None]
                output, error, recover, empty, records, fork = self.run_fixture(outcomes)
                self.assertIsInstance(error, RuntimeError)
                recover.assert_not_called()
                self.assertNotIn('REMOVED_SCOPE=', output)
                self.assertNotIn('_PASS', output)
                if boundary == 0:
                    fork.assert_not_called()
                    self.assertEqual(records, [])
                else:
                    self.assertEqual(empty.call_count, 3)
                    self.assertTrue(all(r.status is not None for r in records))
                    self.assertIn('independent recovery required', output)

    def test_empty_boundary_error_then_echild_cannot_restore_eligibility(self):
        for boundary in range(3):
            for fault in (InterruptedError('injected EINTR'), OSError('unknown wait error')):
                with self.subTest(boundary=boundary, fault=fault):
                    outcomes = [None]*boundary+[fault,None]
                    output, error, recover, empty, records, fork = self.run_fixture(outcomes)
                    self.assertIsNotNone(error)
                    recover.assert_not_called()
                    self.assertNotIn('REMOVED_SCOPE=', output)
                    self.assertNotIn('_PASS', output)
                    if boundary == 0:
                        fork.assert_not_called()
                    else:
                        self.assertEqual(empty.call_count, 3)
                        self.assertTrue(all(r.status is not None for r in records))

    def test_setup_failure_precedes_pipes_fork_and_empty_boundary(self):
        output, error, recover, empty, records, fork = self.run_fixture(
            [], setup_error=RuntimeError('fixture wait setup failed'))
        self.assertIsInstance(error, RuntimeError)
        fork.assert_not_called()
        empty.assert_not_called()
        self.assertEqual(records, [])
        recover.assert_not_called()
        self.assertNotIn('_PASS', output)

    def test_adoption_and_ownership_loss_remain_sticky_after_known_cleanup(self):
        for fault in ('adoption', 'observation'):
            with self.subTest(fault=fault):
                output, error, recover, _, records, _ = self.run_fixture([None,None], fault)
                self.assertIsInstance(error, RuntimeError)
                self.assertTrue(all(r.status is not None and not r.uncertain
                                    for r in records))
                recover.assert_not_called()
                self.assertNotIn('REMOVED_SCOPE=', output)
                self.assertNotIn('_PASS', output)

    def test_functional_assertion_failure_with_intact_ownership_can_recover(self):
        output, error, recover, _, records, _ = self.run_fixture(
            [None,None], assertion_error=RuntimeError('CLI assertion failed'))
        self.assertIsInstance(error, RuntimeError)
        self.assertTrue(all(r.status is not None for r in records))
        self.assertEqual(recover.call_count, 2)
        self.assertIn('REMOVED_SCOPE=', output)
        self.assertNotIn('_PASS', output)

    def test_normal_flow_requires_known_cleanup_before_two_recoveries_and_pass(self):
        output, error, recover, empty, records, _ = self.run_fixture([None,None,None])
        self.assertIsNone(error)
        self.assertEqual(recover.call_count, 2)
        self.assertEqual(empty.call_count, 3)
        self.assertTrue(all(r.status == -probe.signal.SIGKILL for r in records))
        self.assertEqual(output.count('OWNED_CHILD_REAP='), 2)
        self.assertEqual(output.count('OWNED_CHILD_NONEXIT='), 2)
        self.assertLess(output.index('"phase": "before-assertion"'),
                        output.index('"phase": "after-assertion"'))
        self.assertLess(output.index('REMOVED_SCOPE='),
                        output.index('READ_POLICY_COORDINATOR_LOSS_RECOVERY_PASS'))


class ContextObservation(unittest.TestCase):
    def test_only_validated_context_is_printed(self):
        reply = {'stage':'writer-context','label':'fixture-writer',
                 'scope':'root-writer-only'}
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            probe.context(reply, 'key', 'writer', 'fixture-writer')
        self.assertIn('ROLE_CONTEXT=', output.getvalue())
        output = io.StringIO()
        with contextlib.redirect_stdout(output), self.assertRaises(RuntimeError):
            probe.context(reply, 'key', 'writer', 'wrong-label')
        self.assertEqual(output.getvalue(), '')


if __name__ == '__main__':
    unittest.main()
