# SPDX-License-Identifier: Apache-2.0
"""Injected supervisor orchestration, not module/root/reference runtime evidence."""
import contextlib
import io
import json
import hashlib
import os
import stat
import tempfile
from pathlib import Path
import types
import unittest
from unittest.mock import patch


def load(name, path):
    module = types.ModuleType(name)
    module.__file__ = str(path)
    exec(compile(path.read_bytes(), str(path), 'exec'), module.__dict__)
    return module


source = Path(__file__).parents[1]/'integration'
core = load('survivor_core_test_dependency', source/'read_policy_survivor_core.py')
fixture = load('survivor_fixture', source/'read_policy_survivor.py')


def frame(value):
    return json.dumps(value).encode()


class Operations:
    def __init__(self, fail=None):
        self.fail, self.calls, self.removed = fail, [], False
        self.empty_calls = 0

    def call(self, name):
        self.calls.append(name)
        if name == self.fail:
            raise RuntimeError('injected '+name)

    def kill_coordinator(self): self.call('coordinator-reap')
    def adopt_nonexit(self): self.call('adopt-before')
    def observe_nonexit(self): self.call('nonexit-after')
    def wait_role_normal(self): self.call('role-reap')
    def endpoint_absent(self): self.call('endpoint')
    def ex(self, busy): self.call('busy' if busy else 'available')
    def empty(self):
        self.empty_calls += 1
        self.call('post-empty' if self.empty_calls == 1 else 'final-empty')
    def remove(self):
        self.call('remove')
        self.removed = True


class SupervisorProof(unittest.TestCase):
    def test_full_order_requires_actual_reaps_and_both_empty_barriers(self):
        ops, ledger = Operations(), core.Retirement()
        fixture.prove_survival(ops, ledger)
        self.assertEqual(ops.calls, ['coordinator-reap','adopt-before','busy',
                                     'nonexit-after','role-reap','post-empty',
                                     'endpoint','available','final-empty','remove'])
        self.assertTrue(ops.removed)
        self.assertTrue(ledger.eligible())

    def test_each_phase_fault_poison_survives_eventual_known_cleanup(self):
        for stage in ['coordinator-reap','adopt-before','busy','nonexit-after',
                      'role-reap','post-empty','endpoint','available','final-empty']:
            with self.subTest(stage=stage):
                ops, ledger = Operations(stage), core.Retirement()
                with self.assertRaisesRegex(RuntimeError, 'injected'):
                    fixture.prove_survival(ops, ledger)
                self.assertTrue(ledger.uncertain)
                # Fake eventual known cleanup/empty cannot restore eligibility.
                ledger.coordinator_reaped = ledger.role_reaped = True
                ledger.post_empty = ledger.final_empty = ledger.endpoint_absent = True
                self.assertFalse(ledger.eligible())
                self.assertFalse(ops.removed)
                self.assertNotIn('remove', ops.calls)

    def test_prior_uncertainty_prevents_remove_despite_all_later_proofs(self):
        ops, ledger = Operations(), core.Retirement()
        ledger.poison()
        with self.assertRaisesRegex(RuntimeError, 'ineligible'):
            fixture.prove_survival(ops, ledger)
        self.assertNotIn('remove', ops.calls)

    def test_removal_error_remains_failure(self):
        ops, ledger = Operations('remove'), core.Retirement()
        with self.assertRaisesRegex(RuntimeError, 'injected remove'):
            fixture.prove_survival(ops, ledger)
        self.assertTrue(ledger.uncertain)
        self.assertFalse(ops.removed)

    def test_pipe_allocation_failure_retains_scope_and_runs_close(self):
        class Scope:
            done = False
            closed = False
            def close(self):
                self.closed = True
                print('RETAINED_REFERENCE_SURVIVOR_SCOPE=fake')
        scope = Scope()
        children = types.SimpleNamespace(OwnedChild=lambda: types.SimpleNamespace(
            pid=None, uncertain=False, status=None), prepare_wait_boundaries=lambda: None,
            require_no_children=lambda: None)
        modified_core = types.SimpleNamespace(Retirement=core.Retirement,
                                              PipeEnds=lambda: (_ for _ in ()).throw(OSError('pipe')))
        output = io.StringIO()
        with patch.object(fixture, 'Scope', return_value=scope), contextlib.redirect_stdout(output), \
                self.assertRaises(OSError):
            fixture.experiment(modified_core, children, None, None, 'server')
        self.assertTrue(scope.closed)
        self.assertFalse(scope.done)
        self.assertIn('RETAINED_', output.getvalue())
        self.assertNotIn('CASE_PASS', output.getvalue())

    def test_ambiguous_fork_retains_scope_and_closes_all_owned_ipc(self):
        class Scope:
            done = False
            closed = False
            def close(self):
                self.closed = True
                print('RETAINED_REFERENCE_SURVIVOR_SCOPE=fake')
        scope = Scope()
        records = []
        class Child:
            pid = status = None
            uncertain = False
            def __init__(self): records.append(self)
        children = types.SimpleNamespace(OwnedChild=Child,
            prepare_wait_boundaries=lambda: None, require_no_children=lambda: None)
        created = []
        def pipes():
            value = core.PipeEnds()
            created.append(value)
            return value
        modified_core = types.SimpleNamespace(Retirement=core.Retirement, PipeEnds=pipes)
        output = io.StringIO()
        with patch.object(fixture, 'Scope', return_value=scope), \
                patch.object(fixture.os, 'fork', side_effect=MemoryError), \
                contextlib.redirect_stdout(output), self.assertRaises(MemoryError):
            fixture.experiment(modified_core, children, None, None, 'reader')
        self.assertTrue(records[0].uncertain)
        self.assertTrue(scope.closed)
        self.assertTrue(all(fd == -1 for value in created for fd in value.ends))
        self.assertIn('RETAINED_', output.getvalue())
        self.assertNotIn('CASE_PASS', output.getvalue())


class BarrierReports(unittest.TestCase):
    def records(self, mode='server'):
        reports = fixture.BarrierRecords(core, mode)
        spawned = dict(stage='spawned',pid=200,server_pid=201 if mode=='reader' else None,dev=1,ino=2)
        if mode == 'server':
            ack = dict(stage='server-hold',pid=200,listening=True,services=0,
                       cancel=0,other=0,rejected=0,created=0)
            report = dict(stage='server-ready',pid=200,services=0,cancel=0,
                          other=0,rejected=0,created=0)
        else:
            ack = dict(stage='reader-hold',pid=200,role='system301-platform',
                       token='system301-platform:200',reply=-6,teardown=True)
            report = dict(stage='reader-correlated',pid=200,server_pid=201,
                server_reaped=True,server_exit=0,endpoint_absent=True,reply=-6,
                body=dict(stage='server-cancel-body',role='system301-platform',
                          pid=200,uid=301,gid=301,socket_label='System',
                          token='system301-platform:200',cancel_calls=1,other_calls=0))
        return reports,spawned,ack,report

    def test_ack_and_correlation_are_independent_in_either_order(self):
        for mode in ['server','reader']:
            for first in ['role','coordinator']:
                with self.subTest(mode=mode,first=first):
                    r,spawned,ack,report = self.records(mode)
                    r.observe('coordinator',frame(spawned))
                    r.observe(first,frame(ack if first=='role' else report))
                    self.assertFalse(r.ready())
                    second = 'coordinator' if first=='role' else 'role'
                    r.observe(second,frame(report if second=='coordinator' else ack))
                    self.assertTrue(r.ready())

    def test_pre_spawn_ack_is_bounded_and_not_authority(self):
        r,spawned,ack,report = self.records()
        r.observe('role',frame(ack))
        self.assertIsNone(r.pid)
        self.assertFalse(r.ready())
        r.observe('coordinator',frame(spawned))
        self.assertFalse(r.ready())
        r.observe('coordinator',frame(report))
        self.assertTrue(r.ready())

    def test_reader_body_wrong_pid_or_boolean_counter_fails(self):
        for field,value in [('pid',201),('cancel_calls',True)]:
            r,spawned,ack,report = self.records('reader')
            report['body'][field]=value
            r.observe('coordinator',frame(spawned))
            r.observe('role',frame(ack))
            with self.assertRaises(core.Failure):
                r.observe('coordinator',frame(report))
            self.assertFalse(r.ready())

    def test_late_cross_pipe_diagnostic_adds_no_barrier_authority(self):
        r,spawned,ack,report = self.records('reader')
        r.observe('coordinator',frame(spawned))
        r.observe('role',frame(ack))
        r.observe('coordinator',frame(report))
        with contextlib.redirect_stdout(io.StringIO()):
            r.observe('server',b'REAL_GATE_BODY=earlier-source-validated-report')
        self.assertTrue(r.ready())
        with self.assertRaises(core.Failure): r.observe('role',frame(ack))
        self.assertFalse(r.ready())

    def test_stderr_or_extra_server_frames_fail(self):
        for origin,line in [('error',b'failure'),('server-error',b'failure'),
                            ('server',b'{}')]:
            r,_,_,_ = self.records('reader')
            with self.assertRaises((RuntimeError,core.Failure)):
                r.observe(origin,line)


    def test_unsupported_native_diagnostic_reports_no_authorization_progress(self):
        reports, _, _, _ = self.records('reader')
        line = (b'REAL_GATE_NOT_PROVED role=system301-platform stage=client-connect '
                b'native=InvalidProtocolException specific_Cynara_decision=NOT_OBSERVED')
        with self.assertRaises(RuntimeError) as error:
            reports.observe('role', line)
        message = str(error.exception)
        self.assertIn("mode='reader' origin='role'", message)
        self.assertIn('bytes=' + str(len(line)), message)
        self.assertIn('prefix=' + repr(line), message)
        self.assertIn('truncated=False', message)
        self.assertFalse(reports.ready())
        self.assertIsNone(reports.pid)
        self.assertEqual(reports.pending, [])

    def test_known_native_diagnostic_on_wrong_origin_remains_refused(self):
        for origin, line in [('role', b'REAL_GATE_BODY=not-a-reader-reply'),
                             ('server', b'REAL_GATE_CLIENT_REPLY role=system301-platform')]:
            with self.subTest(origin=origin):
                reports, _, _, _ = self.records('reader')
                with self.assertRaises(RuntimeError) as error:
                    reports.observe(origin, line)
                self.assertIn('origin=' + repr(origin), str(error.exception))
                self.assertFalse(reports.ready())
                self.assertIsNone(reports.barrier)

    def test_refused_native_diagnostic_escapes_invalid_utf8_and_controls(self):
        reports, _, _, _ = self.records('reader')
        line = b'REAL_GATE_UNKNOWN=\xff\n\r\t\x00\x1b[2J'
        with self.assertRaises(RuntimeError) as error:
            reports.observe('role', line)
        message = str(error.exception)
        self.assertIn(repr(line), message)
        for control in ('\n', '\r', '\t', '\x00', '\x1b'):
            self.assertNotIn(control, message)
        self.assertFalse(reports.ready())

    def test_refused_native_diagnostic_reports_length_and_explicit_truncation(self):
        reports, _, _, _ = self.records('reader')
        line = b'REAL_GATE_UNKNOWN=' + b'\xff' * 1024 + b'UNPRINTED_TAIL'
        with self.assertRaises(RuntimeError) as error:
            reports.observe('role', line)
        message = str(error.exception)
        self.assertIn('bytes=' + str(len(line)), message)
        self.assertIn('prefix=' + repr(line[:256]), message)
        self.assertIn('truncated=True', message)
        self.assertNotIn('UNPRINTED_TAIL', message)
        self.assertLess(len(message), 1200)
        self.assertFalse(reports.ready())
        self.assertEqual(reports.pending, [])


class PublicationBoundary(unittest.TestCase):
    def test_pending_create_and_partial_write_do_not_trigger_a_record_read(self):
        final = types.SimpleNamespace(exists=unittest.mock.Mock(side_effect=[False,False,True]))
        reads = []
        def complete(path):
            self.assertEqual(final.exists.call_count,3)
            reads.append(path)
            return b'{"stage":"ready"}'
        with patch.object(fixture,'bounded_json',side_effect=complete), \
                patch.object(fixture.time,'sleep') as sleep:
            self.assertEqual(fixture.published_record(core,final),{'stage':'ready'})
        self.assertEqual(reads,[final])
        self.assertEqual(sleep.call_count,2)

    def test_failed_publication_budget_never_reads_pending_or_returns_ready(self):
        final=types.SimpleNamespace(exists=lambda:False)
        ticks=iter([1,1,0])
        class Budget:
            def remaining(self):
                if not next(ticks):raise core.Failure('publication budget')
                return 1
        dependency=types.SimpleNamespace(Deadline=lambda seconds:Budget(),decode=core.decode)
        with patch.object(fixture,'bounded_json') as read,patch.object(fixture.time,'sleep'), \
                self.assertRaisesRegex(core.Failure,'publication budget'):
            fixture.published_record(dependency,final)
        read.assert_not_called()

    def test_final_read_decode_past_budget_is_not_success(self):
        ticks=iter([1,0])
        class Budget:
            def remaining(self):
                if not next(ticks):raise core.Failure('late publication read')
                return 1
        dependency=types.SimpleNamespace(Deadline=lambda seconds:Budget(),decode=core.decode)
        final=types.SimpleNamespace(exists=lambda:True)
        with patch.object(fixture,'bounded_json',return_value=b'{"stage":"ready"}'), \
                self.assertRaisesRegex(core.Failure,'late publication read'):
            fixture.published_record(dependency,final)


class SourceLoading(unittest.TestCase):
    def sample(self, info, **changes):
        fields = dict(st_dev=info.st_dev, st_ino=info.st_ino, st_mode=stat.S_IFREG | 0o600,
                      st_uid=0, st_gid=0, st_nlink=1, st_size=info.st_size,
                      st_mtime_ns=info.st_mtime_ns, st_ctime_ns=info.st_ctime_ns,
                      st_atime_ns=info.st_atime_ns)
        fields.update(changes)
        return types.SimpleNamespace(**fields)

    def test_atime_change_loads_exact_source_without_cache(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'source.py'
            data = b'validated_source = True\n'
            path.write_bytes(data)
            info = path.stat()
            before = self.sample(info)
            after = self.sample(info, st_atime_ns=info.st_atime_ns+9999)
            with patch.object(fixture, 'trusted'), \
                    patch.object(fixture.os, 'fstat', side_effect=[before,after]), \
                    patch.object(fixture.Path, 'lstat', return_value=after):
                module = fixture.source_module(path, hashlib.sha256(data).hexdigest())
            self.assertTrue(module.validated_source)
            self.assertFalse((path.parent/'__pycache__').exists())

    def test_changed_pinned_or_named_source_refuses_before_compile(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'source.py'
            data = b'validated_source = True\n'
            path.write_bytes(data)
            info = path.stat()
            baseline = self.sample(info)
            for field in ('st_dev','st_ino','st_mode','st_uid','st_gid','st_nlink',
                          'st_size','st_mtime_ns','st_ctime_ns'):
                for changed_where in ('held','named'):
                    changed = self.sample(info, **{field:getattr(baseline,field)+1})
                    held = changed if changed_where=='held' else baseline
                    named = changed if changed_where=='named' else baseline
                    with self.subTest(field=field,where=changed_where), \
                            patch.object(fixture,'trusted'), \
                            patch.object(fixture.os,'fstat',side_effect=[baseline,held]), \
                            patch.object(fixture.Path,'lstat',return_value=named), \
                            patch('builtins.compile') as compiler, self.assertRaises(RuntimeError):
                        fixture.source_module(path,hashlib.sha256(data).hexdigest())
                    compiler.assert_not_called()

    def test_wrong_digest_refuses_before_compile(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'source.py'
            path.write_bytes(b'validated_source = True\n')
            baseline = self.sample(path.stat())
            with patch.object(fixture,'trusted'), \
                    patch.object(fixture.os,'fstat',return_value=baseline), \
                    patch.object(fixture.Path,'lstat',return_value=baseline), \
                    patch('builtins.compile') as compiler, self.assertRaisesRegex(RuntimeError,'digest'):
                fixture.source_module(path,'0'*64)
            compiler.assert_not_called()


if __name__ == '__main__':
    unittest.main()
