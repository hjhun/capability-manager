# SPDX-License-Identifier: Apache-2.0
"""Ordinary protocol/pipe tests, no module, SH holder or context-drop route."""
import errno
import json
import os
from pathlib import Path
import select
import signal
import time
import types
import unittest

SOURCE = Path(__file__).parents[1]/'integration/read_policy_survivor_core.py'
core = types.ModuleType('survivor_core')
core.__file__ = str(SOURCE)
exec(compile(SOURCE.read_bytes(), str(SOURCE), 'exec'), core.__dict__)


def line(value):
    return json.dumps(value, separators=(',', ':')).encode()


def reader_ack(pid=111):
    return dict(stage='reader-hold', pid=pid, role='system301-platform',
                token='system301-platform:' + str(pid), reply=-6, teardown=True)


def reader_record(pid=111, server=112):
    return dict(stage='reader-correlated', pid=pid, server_pid=server,
                server_reaped=True, server_exit=0, endpoint_absent=True,
                reply=-6, body=dict(stage='server-cancel-body',
                                   role='system301-platform', pid=pid, uid=301,
                                   gid=301, socket_label='System',
                                   token='system301-platform:' + str(pid),
                                   cancel_calls=1, other_calls=0))


class Clock:
    def __init__(self, step=0.1):
        self.now, self.tick = -step, step
    def __call__(self):
        self.now += self.tick
        return self.now


class Transport(unittest.TestCase):
    def test_final_full_write_after_budget_fails_without_rollback_claim(self):
        now, written = [0], []
        budget = core.Deadline(1, lambda: now[0])
        def write(fd, data):
            written.append(data)
            now[0] = 2
            return len(data)
        with self.assertRaises(core.Failure):
            core.send(6, b'ACK\n', budget,
                      ready=lambda *a: ([], [6], []), write=write)
        self.assertEqual(written, [b'ACK\n'])

    def test_final_ack_read_after_budget_returns_no_records(self):
        read_fd, write_fd = os.pipe2(os.O_CLOEXEC)
        try:
            output = core.Output({'role': read_fd})
            now, returned = [0], []
            def read(*args):
                now[0] = 2
                return b'ACK\n'
            with self.assertRaises(core.Failure):
                returned += output.step(core.Deadline(1, lambda: now[0]),
                                        ready=lambda *a: ([read_fd], [], []),
                                        read=read)
            self.assertEqual(returned, [])
            self.assertEqual(output.bytes['role'], b'ACK\n')
        finally:
            os.close(read_fd); os.close(write_fd)

    def test_final_eof_after_budget_cannot_succeed(self):
        read_fd, write_fd = os.pipe2(os.O_CLOEXEC)
        try:
            output = core.Output({'role': read_fd})
            now = [0]
            def read(*args):
                now[0] = 2
                return b''
            with self.assertRaises(core.Failure):
                output.step(core.Deadline(1, lambda: now[0]),
                            ready=lambda *a: ([read_fd], [], []), read=read)
            self.assertFalse(output.live)  # failure does not roll back IO
        finally:
            os.close(read_fd); os.close(write_fd)

    def test_both_sides_nonblocking_flags_survive_fixed_source_copies(self):
        pipes = core.PipeEnds()
        duplicates = []
        try:
            self.assertTrue(all(not os.get_blocking(fd) for fd in pipes.ends))
            for index in (0, 3, 5):
                duplicates.append(os.dup(pipes.ends[index]))
            self.assertTrue(all(not os.get_blocking(fd) for fd in duplicates))
        finally:
            for fd in duplicates: os.close(fd)
            pipes.close()

    def test_continually_ready_partial_writes_expire(self):
        writes = []
        def write(fd, data):
            writes.append(data[:1])
            return 1
        clock = Clock()
        with self.assertRaises(core.Failure):
            core.send(6, b'x' * 20, core.Deadline(1, clock),
                      ready=lambda *a: ([], [6], []), write=write)
        self.assertLess(len(writes), 20)

    def test_ready_then_expired_does_not_write(self):
        clock = iter([0, 0.1, 2])
        writes = []
        with self.assertRaises(core.Failure):
            core.send(6, b'x', core.Deadline(1, lambda: next(clock)),
                      ready=lambda *a: ([], [6], []),
                      write=lambda *a: writes.append(a))
        self.assertEqual(writes, [])

    def test_ready_then_eagain_stays_bounded(self):
        calls = []
        def write(*args):
            calls.append(args)
            raise BlockingIOError(errno.EAGAIN, 'injected')
        with self.assertRaises(core.Failure):
            core.send(6, b'x', core.Deadline(1, Clock()),
                      ready=lambda *a: ([], [6], []), write=write)
        self.assertGreater(len(calls), 0)
        self.assertLess(len(calls), 10)

    def test_output_partial_frames_and_eof(self):
        read_fd, write_fd = os.pipe2(os.O_CLOEXEC)
        try:
            output = core.Output({'role': read_fd})
            os.write(write_fd, b'fir')
            self.assertEqual(output.step(core.Deadline(1)), [])
            os.write(write_fd, b'st\nsecond\n')
            self.assertEqual(output.step(core.Deadline(1)),
                             [('role', b'first'), ('role', b'second')])
            os.close(write_fd); write_fd = -1
            self.assertEqual(output.step(core.Deadline(1)), [])
            self.assertFalse(output.live)
        finally:
            os.close(read_fd)
            if write_fd >= 0: os.close(write_fd)

    def test_eagain_output_does_not_extend_budget(self):
        read_fd, write_fd = os.pipe2(os.O_CLOEXEC)
        try:
            output = core.Output({'role': read_fd})
            budget = core.Deadline(1, Clock())
            def read(*args):
                raise BlockingIOError(errno.EAGAIN, 'injected')
            with self.assertRaises(core.Failure):
                while True:
                    output.step(budget, ready=lambda *a: ([read_fd], [], []),
                                read=read)
        finally:
            os.close(read_fd); os.close(write_fd)

    def test_aggregate_and_stream_backpressure_limits(self):
        for per_stream, total, size in [(32, 64, 33), (64, 32, 33)]:
            read_fd, write_fd = os.pipe2(os.O_CLOEXEC)
            try:
                output = core.Output({'role': read_fd}, per_stream=per_stream,
                                     total=total)
                os.write(write_fd, b'x' * size)
                with self.assertRaises(core.Failure):
                    output.step(core.Deadline(1))
            finally:
                os.close(read_fd); os.close(write_fd)

    def test_truncated_frame_eof_rejects(self):
        read_fd, write_fd = os.pipe2(os.O_CLOEXEC)
        try:
            output = core.Output({'role': read_fd})
            os.write(write_fd, b'incomplete')
            output.step(core.Deadline(1))
            os.close(write_fd); write_fd = -1
            with self.assertRaises(core.Failure): output.step(core.Deadline(1))
        finally:
            os.close(read_fd)
            if write_fd >= 0: os.close(write_fd)

    def test_hup_keeps_supervisor_output_until_exact_owned_reap(self):
        # Fresh ordinary Python child, no SQLite/module/reference file exists.
        # Proves pipe copy ownership only, not acknowledged-hold/kernel SH proof.
        pipes = core.PipeEnds()
        pid = None
        reaped = False
        try:
            pid = os.fork()
            if pid == 0:
                try:
                    pipes.coordinator_after_fork()
                    end = core.Deadline(3)
                    while True:
                        ready, _, _ = select.select([pipes.ends[0]], [], [], end.remaining())
                        end.remaining()
                        if not ready: os._exit(2)
                        try: data = os.read(pipes.ends[0], 1)
                        except BlockingIOError: continue
                        if data != b'': os._exit(2)
                        break
                    os.write(pipes.ends[3], b'OUTPUT_AFTER_CONTROL_HUP\n')
                    pipes.coordinator_after_positive_spawn()
                    os._exit(0)
                except BaseException: os._exit(3)
            pipes.supervisor_after_fork()
            pipes.supervisor_control_hup()
            output = core.Output({'role': pipes.ends[2], 'error': pipes.ends[4]})
            records = []
            budget = core.Deadline(3)
            while output.live: records += output.step(budget)
            self.assertEqual(records, [('role', b'OUTPUT_AFTER_CONTROL_HUP')])
            end = time.monotonic() + 3
            while time.monotonic() < end:
                found, status = os.waitpid(pid, os.WNOHANG)
                if found == pid:
                    reaped = True
                    self.assertTrue(os.WIFEXITED(status))
                    self.assertEqual(os.WEXITSTATUS(status), 0)
                    break
                time.sleep(.001)
            self.assertTrue(reaped)
        finally:
            # Exact direct-child authority only. Never signal a reported PID.
            if pid and not reaped:
                found, status = os.waitpid(pid, os.WNOHANG)
                if found == pid:
                    reaped = True
                else:
                    os.kill(pid, signal.SIGKILL)
                    end = time.monotonic() + 3
                    while time.monotonic() < end:
                        found, status = os.waitpid(pid, os.WNOHANG)
                        if found == pid:
                            reaped = True
                            break
                        time.sleep(.001)
                if not reaped:
                    # No scope exists; retain output ends on unconfirmed cleanup.
                    raise core.Failure('retained ordinary pipe-child ownership')
            pipes.close()


class Reports(unittest.TestCase):
    def test_ack_and_coordinator_record_both_required(self):
        for order in [('role', 'coordinator'), ('coordinator', 'role')]:
            barrier = core.Barrier('reader', 111, 112)
            records = {'role': reader_ack(), 'coordinator': reader_record()}
            barrier.observe(order[0], line(records[order[0]]))
            self.assertFalse(barrier.ready())
            barrier.observe(order[1], line(records[order[1]]))
            self.assertTrue(barrier.ready())
            # This says nothing about final actual reap/cleanup.
            self.assertFalse(core.Retirement().eligible())

    def test_zero_activity_registered_server(self):
        ack = dict(stage='server-hold', pid=111, listening=True, services=0,
                   cancel=0, other=0, rejected=0, created=0)
        record = {**ack, 'stage': 'server-ready'}
        del record['listening']
        barrier = core.Barrier('server', 111)
        barrier.observe('role', line(ack))
        barrier.observe('coordinator', line(record))
        self.assertTrue(barrier.ready())
        for key in ('services', 'cancel', 'other', 'rejected', 'created'):
            bad = core.Barrier('server', 111)
            with self.assertRaises(core.Failure):
                bad.observe('role', line({**ack, key: 1}))
            self.assertFalse(bad.ready())

    def test_reader_correlation_and_server_retirement_mismatch(self):
        mutations = [dict(pid=99), dict(reply=0), dict(server_pid=99),
                     dict(server_reaped=False), dict(server_exit=1),
                     dict(endpoint_absent=False)]
        for change in mutations:
            barrier = core.Barrier('reader', 111, 112)
            with self.assertRaises(core.Failure):
                barrier.observe('coordinator', line({**reader_record(), **change}))
            self.assertTrue(barrier.failed)
        for key, value in [('pid', 99), ('uid', 1), ('gid', 1),
                           ('socket_label', 'User::Shell'), ('token', 'wrong'),
                           ('cancel_calls', True), ('other_calls', False)]:
            bad = reader_record(); bad['body'][key] = value
            with self.assertRaises(core.Failure):
                core.Barrier('reader', 111, 112).observe('coordinator', line(bad))

    def test_duplicate_extra_and_nonobject_records_reject(self):
        for data in [b'{"pid":111,"pid":111}', b'[]', b'null', b'\xff']:
            with self.assertRaises(core.Failure): core.decode(data)
        barrier = core.Barrier('reader', 111, 112)
        barrier.observe('role', line(reader_ack()))
        with self.assertRaises(core.Failure):
            barrier.observe('role', line(reader_ack()))
        with self.assertRaises(core.Failure):
            barrier.observe('coordinator', line(reader_record()))
        self.assertFalse(barrier.ready())
        with self.assertRaises(core.Failure):
            core.Barrier('reader', 111, 112).observe(
                'role', line({**reader_ack(), 'exit': 0}))

    def test_eof_or_ack_never_substitutes_for_coordinator_record(self):
        barrier = core.Barrier('reader', 111, 112)
        barrier.observe('role', line(reader_ack()))
        self.assertFalse(barrier.ready())
        with self.assertRaises(core.Failure): barrier.observe('coordinator', b'')
        self.assertFalse(barrier.ready())

    def test_final_reaps_and_both_echild_boundaries_required(self):
        proof = core.Retirement()
        proof.endpoint_absent = True
        proof.reap('coordinator', -1, 9)
        self.assertFalse(proof.eligible())
        proof.reap('role', 0, 0)
        self.assertFalse(proof.eligible())
        proof.empty('post', errno.ECHILD)
        self.assertFalse(proof.eligible())
        proof.empty('final', errno.ECHILD)
        self.assertTrue(proof.eligible())

    def test_later_empty_or_known_reaps_do_not_restore_uncertainty(self):
        for lost_at in ('before', 'post', 'final'):
            proof = core.Retirement()
            proof.endpoint_absent = True
            if lost_at == 'before': proof.poison()
            proof.reap('coordinator', -1, 9)
            proof.reap('role', 0, 0)
            if lost_at == 'post':
                with self.assertRaises(core.Failure): proof.empty('post', 0)
            proof.empty('post', errno.ECHILD)
            if lost_at == 'final':
                with self.assertRaises(core.Failure): proof.empty('final', errno.EINTR)
            proof.empty('final', errno.ECHILD)
            self.assertFalse(proof.eligible())

    def test_signal_or_nonzero_role_exit_cannot_be_clean_retirement(self):
        for code, sig in [(20, 0), (-1, 9), (False, 0)]:
            proof = core.Retirement()
            with self.assertRaises(core.Failure): proof.reap('role', code, sig)
            self.assertFalse(proof.eligible())


if __name__ == '__main__':
    unittest.main()
