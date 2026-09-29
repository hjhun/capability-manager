# SPDX-License-Identifier: Apache-2.0
"""Private non-policy survivor protocol/pipe core, not a workload entry point.

No FD/PID supplied by these records grants signal, wait, lock or cleanup authority.
The future fixed supervisor must independently own/reap the coordinator and prove
P_PID adoption/nonexit for the expected survivor. Validated reports are only its
barrier inputs. In particular ACK, EOF and lock contention never prove exit.
"""
import errno
import json
import os
import select
import time


class Failure(RuntimeError):
    pass


class Deadline:
    """Retry/processing deadline, not a hard bound on arbitrary filesystem IO."""
    def __init__(self, seconds, clock=time.monotonic):
        if not 0 < seconds <= 60:
            raise ValueError('fixed positive bounded budget required')
        self.clock = clock
        self.end = clock() + seconds

    def remaining(self):
        left = self.end - self.clock()
        if left <= 0:
            raise Failure('absolute deadline elapsed')
        return left


def send(fd, data, deadline, *, ready=select.select, write=os.write):
    if not isinstance(data, bytes) or not 0 < len(data) <= 4096:
        raise Failure('bounded control frame required')
    offset = 0
    while offset < len(data):
        left = deadline.remaining()
        try:
            _, writable, _ = ready([], [fd], [], left)
        except InterruptedError:
            continue
        deadline.remaining()  # readiness cannot extend the fixed budget
        if fd not in writable:
            raise Failure('control readiness deadline')
        try:
            count = write(fd, data[offset:])
        except (InterruptedError, BlockingIOError):
            continue
        if not isinstance(count, int) or not 0 < count <= len(data) - offset:
            raise Failure('control partial write failed')
        offset += count
        deadline.remaining()  # completed bytes are not rolled back on expiry


class Output:
    """Supervisor-owned read ends, retained/drained through final role exit.

    Reads are nonblocking and bounded by stream, aggregate, frame and absolute
    budget. Closing an output reader belongs after exact role cleanup; coordinator
    exit/EOF alone does not transfer that obligation or prove the role exited.
    """
    def __init__(self, streams, *, per_stream=8192, total=24576):
        if (not streams or len(streams) > 5 or
                len(set(streams.values())) != len(streams) or
                not 0 < per_stream <= 8192 or not 0 < total <= 24576):
            raise Failure('fixed distinct bounded output table required')
        self.streams = dict(streams)
        self.live = set(streams)
        self.bytes = {name: bytearray() for name in streams}
        self.partial = {name: bytearray() for name in streams}
        self.per_stream, self.limit, self.total = per_stream, total, 0
        for fd in streams.values():
            os.set_blocking(fd, False)

    def step(self, deadline, *, ready=select.select, read=os.read):
        left = deadline.remaining()
        if not self.live:
            deadline.remaining()
            return []
        fds = [self.streams[name] for name in self.live]
        try:
            readable, _, _ = ready(fds, [], [], left)
        except InterruptedError:
            deadline.remaining()
            return []
        deadline.remaining()
        if not readable:
            raise Failure('output readiness deadline')
        records = []
        for name in tuple(sorted(self.live)):
            if self.streams[name] not in readable:
                continue
            deadline.remaining()
            try:
                data = read(self.streams[name], 4096)
            except (InterruptedError, BlockingIOError):
                continue
            if not isinstance(data, bytes) or len(data) > 4096:
                raise Failure('invalid bounded read result')
            if not data:
                self.live.remove(name)
                if self.partial[name]:
                    raise Failure('truncated output frame at EOF')
                continue
            self.total += len(data)
            self.bytes[name].extend(data)
            self.partial[name].extend(data)
            if len(self.bytes[name]) > self.per_stream or self.total > self.limit:
                raise Failure('output byte limit')
            while b'\n' in self.partial[name]:
                line, _, rest = self.partial[name].partition(b'\n')
                self.partial[name] = bytearray(rest)
                if not line or len(line) > 4096 or len(records) >= 64:
                    raise Failure('output frame/processing limit')
                records.append((name, bytes(line)))
            if len(self.partial[name]) > 4096:
                raise Failure('partial frame limit')
        deadline.remaining()  # includes final frame/EOF processing
        return records


class PipeEnds:
    """Allocate before supervisor fork and before the SH file exists.

    No reference descriptor is stored here. Both processes close their exact
    opposite ends immediately after fork. Supervisor retains output readers and
    the control writer; coordinator keeps only spawn sources, closes them after
    positive role attachment. Only the fixed mapping helper maps role0..2 and4.
    """
    def __init__(self):
        self.ends = []
        try:
            for _ in range(3):
                self.ends.extend(os.pipe2(os.O_CLOEXEC))
            # control-read/write, output-read/write, error-read/write
            # Separate read/write descriptions: read-side NONBLOCK does not
            # affect the opposite writer. Fixed spawn copies preserve these.
            for index in range(6):
                os.set_blocking(self.ends[index], False)
                if os.get_blocking(self.ends[index]):
                    raise Failure('fixed endpoint NONBLOCK verification failed')
        except BaseException:
            self.close()
            raise

    def close_indices(self, indices):
        errors = []
        for index in indices:
            fd = self.ends[index]
            if fd < 0:
                errors.append('already closed fixed endpoint')
                continue
            self.ends[index] = -1
            try:
                os.close(fd)
            except OSError as error:
                errors.append(str(error))
        if errors:
            raise Failure('; '.join(errors))

    def supervisor_after_fork(self):
        self.close_indices((0, 3, 5))

    def coordinator_after_fork(self):
        self.close_indices((1, 2, 4))

    def supervisor_control_hup(self):
        self.close_indices((1,))

    def coordinator_after_positive_spawn(self):
        self.close_indices((0, 3, 5))

    def close(self):
        errors = []
        for index, fd in enumerate(self.ends):
            if fd < 0:
                continue
            self.ends[index] = -1
            try:
                os.close(fd)
            except OSError as error:
                errors.append(str(error))
        if errors:
            raise Failure('; '.join(errors))


def exact(value, expected):
    # bool is an int subclass in Python: equality alone is insufficient.
    if type(value) is not type(expected):
        return False
    if type(expected) is dict:
        return value.keys() == expected.keys() and all(
            exact(value[key], item) for key, item in expected.items())
    return value == expected


def decode(line):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise Failure('duplicate protocol field')
            result[key] = value
        return result
    try:
        if not isinstance(line, bytes) or not 0 < len(line) <= 4096:
            raise Failure('bounded protocol line required')
        value = json.loads(line.decode('utf-8'), object_pairs_hook=pairs)
        if type(value) is not dict:
            raise Failure('protocol object required')
        return value
    except (UnicodeError, ValueError) as error:
        raise Failure('invalid protocol JSON') from error


class Barrier:
    """Require BOTH independent role ACK and coordinator correlated record.

    All expected PIDs are fixed positive spawn results retained by the future
    coordinator; no record may select a PID. This class does not establish kernel
    child ownership, adoption, sole-holder/liveness or final termination.
    """
    def __init__(self, mode, survivor, server=None):
        if (mode not in ('server', 'reader') or type(survivor) is not int or
                survivor <= 0 or (mode == 'reader' and
                (type(server) is not int or server <= 0 or server == survivor))):
            raise Failure('fixed known role identities required')
        self.mode, self.pid, self.server = mode, survivor, server
        self.ack = self.coordinator = False
        self.failed = False

    def require(self, value, expected):
        if (type(value) is not dict or value.keys() != expected.keys() or
                any(not exact(value[key], item) for key, item in expected.items())):
            raise Failure('fixed correlated report mismatch')

    def observe(self, origin, line):
        if self.failed:
            raise Failure('barrier already failed')
        try:
            record = decode(line)
            if origin == 'role' and not self.ack:
                expected = {'stage': self.mode + '-hold', 'pid': self.pid}
                if self.mode == 'reader':
                    expected.update(role='system301-platform',
                                    token='system301-platform:' + str(self.pid),
                                    reply=-6, teardown=True)
                else:
                    expected.update(listening=True, services=0, cancel=0,
                                    other=0, rejected=0, created=0)
                self.require(record, expected)
                self.ack = True
            elif origin == 'coordinator' and not self.coordinator:
                if self.mode == 'server':
                    self.require(record, {'stage': 'server-ready', 'pid': self.pid,
                                          'services': 0, 'cancel': 0, 'other': 0,
                                          'rejected': 0, 'created': 0})
                else:
                    self.require(record, {
                        'stage': 'reader-correlated', 'pid': self.pid,
                        'server_pid': self.server, 'server_reaped': True,
                        'server_exit': 0, 'endpoint_absent': True, 'reply': -6,
                        'body': {'stage': 'server-cancel-body',
                                 'role': 'system301-platform', 'pid': self.pid,
                                 'uid': 301, 'gid': 301, 'socket_label': 'System',
                                 'token': 'system301-platform:' + str(self.pid),
                                 'cancel_calls': 1, 'other_calls': 0}})
                self.coordinator = True
            else:
                raise Failure('duplicate/unexpected barrier origin')
        except BaseException:
            self.failed = True
            raise

    def ready(self):
        return self.ack and self.coordinator and not self.failed


class Retirement:
    """Explicit final proof ledger; never restores lost cleanup eligibility."""
    def __init__(self):
        self.uncertain = False
        self.coordinator_reaped = self.role_reaped = False
        self.post_empty = self.final_empty = False
        self.endpoint_absent = False

    def poison(self):
        self.uncertain = True

    def reap(self, kind, code, signal):
        # Inputs are actual exact-owned wait results, not role reports/EOF.
        if (kind not in ('coordinator', 'role') or type(code) is not int or
                type(signal) is not int or
                (kind == 'coordinator' and (code != -1 or signal != 9)) or
                (kind == 'role' and (code != 0 or signal != 0))):
            self.poison()
            raise Failure('unexpected exact-owned termination')
        field = kind + '_reaped'
        if getattr(self, field):
            self.poison()
            raise Failure('duplicate reap proof')
        setattr(self, field, True)

    def empty(self, boundary, error):
        # Actual expected-empty P_ALL must return ECHILD, never None/no-event.
        if (boundary not in ('post', 'final') or type(error) is not int or
                error != errno.ECHILD or
                not (self.coordinator_reaped and self.role_reaped) or
                (boundary == 'final' and not self.post_empty)):
            self.poison()
            raise Failure('expected-empty proof failed')
        setattr(self, boundary + '_empty', True)

    def eligible(self):
        return (not self.uncertain and self.coordinator_reaped and
                self.role_reaped and self.post_empty and self.final_empty and
                self.endpoint_absent)
