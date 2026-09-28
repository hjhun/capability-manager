#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Explicit root fixture: fixed root writer/UID301 readers, modeled C handoff.

Private transport only: no CLI, provisioning, policy writes or SQLite in this
coordinator. Caller supplies a fixed previously validated fixture image and a
retained inherited SH journal FD, preallocates/stores Role BEFORE spawn, and keeps
all slots until cleanup is proven. Native role execution requires separate safety
review. It does not prove UID/label setup from a merely parsed context reply.
"""
import errno
import fcntl
import json
import os
from pathlib import Path
import select
import signal
import time

def require(value, cause):
    if not value:
        raise RuntimeError(cause)


def task_label():
    label = Path('/proc/self/attr/current').read_bytes().rstrip(b'\0\n').decode()
    require(0 < len(label) <= 255 and not any(x.isspace() for x in label),
            'observed writer label')
    return label


def message(fd, seconds=20):
    end = time.monotonic()+seconds
    data = bytearray()
    while time.monotonic() < end:
        ready, _, _ = select.select([fd], [], [], max(0, end-time.monotonic()))
        require(ready, 'role reply deadline')
        byte = os.read(fd, 1)
        require(byte, 'role reply EOF')
        if byte == b'\n':
            def unique(pairs):
                result = {}
                for key, value in pairs:
                    require(key not in result, 'duplicate role reply key')
                    result[key] = value
                return result
            result = json.loads(data, object_pairs_hook=unique)
            require(isinstance(result, dict) and result.get('stage') != 'FAIL',
                    'role setup/operation failed: '+repr(result))
            return result
        data.extend(byte)
        require(len(data) < 4096, 'role reply size')
    raise RuntimeError('role reply deadline')


def send(fd, value, seconds=3):
    data = (json.dumps(value, separators=(',', ':'))+'\n').encode()
    require(len(data) <= 4096, 'command size')
    end = time.monotonic()+seconds
    offset = 0
    while offset != len(data):
        remaining = end-time.monotonic()
        require(remaining > 0, 'command backpressure deadline')
        _, ready, _ = select.select([], [fd], [], remaining)
        require(ready, 'command backpressure deadline')
        require(time.monotonic() < end, 'command backpressure deadline')
        try:
            written = os.write(fd, data[offset:])
        except BlockingIOError:
            continue
        require(written > 0, 'command write')
        offset += written


class Role:
    """Exclusive unreaped direct child, fixed image/FD map; no SQLite in parent."""
    def __init__(self):
        # Caller stores this object in a preallocated slot BEFORE spawn.
        self.pid = None
        self.command = self.reply = -1
        self.status = None
        self.uncertain_spawn = False

    def spawn(self, image, recovery_fd, config):
        require(len(list(Path('/proc/self/task').iterdir())) == 1,
                'single-thread fixture coordinator required')
        require(self.pid is None and not self.uncertain_spawn,
                'role slot already attempted')
        sources = []
        pipes = []
        prior_mask = None
        try:
            cr, cw = os.pipe2(os.O_CLOEXEC)
            pipes.extend([cr, cw])
            rr, rw = os.pipe2(os.O_CLOEXEC)
            pipes.extend([rr, rw])
            for fd in (cr, rw, recovery_fd):
                sources.append(fcntl.fcntl(fd, fcntl.F_DUPFD_CLOEXEC, 6))
            actions = [(os.POSIX_SPAWN_OPEN, 0, '/dev/null', os.O_RDONLY, 0),
                       (os.POSIX_SPAWN_OPEN, 1, '/dev/null', os.O_WRONLY, 0),
                       (os.POSIX_SPAWN_OPEN, 2, '/dev/null', os.O_WRONLY, 0)]
            actions += [(os.POSIX_SPAWN_DUP2, fd, target)
                        for fd, target in zip(sources, (3, 4, 5))]
            # No raising Python handler may interrupt the FD snapshot or the
            # spawn/PID assignment window. Child explicitly starts unmasked.
            # SIGKILL still means abrupt coordinator loss: inherited journal SH
            # must prevent recovery while a role survives it.
            blocked = signal.valid_signals()-{signal.SIGKILL, signal.SIGSTOP}
            prior_mask = signal.pthread_sigmask(signal.SIG_BLOCK, blocked)
            # Python's target closefrom constant is not assumed. With a verified
            # single-thread parent, snapshot all own live FDs after every dup;
            # no later parent FD operation occurs before spawn. The child checks
            # exact0..5 independently. This is not an untrusted peer PID proof.
            opened = []
            for entry in Path('/proc/self/fd').iterdir():
                fd = int(entry.name)
                if fd < 6:
                    continue
                try:
                    fcntl.fcntl(fd, fcntl.F_GETFD)
                except OSError as error:
                    require(error.errno == errno.EBADF, 'parent FD scan')
                    continue
                opened.append(fd)
            actions += [(os.POSIX_SPAWN_CLOSE, fd) for fd in opened]
            # A Python allocation error after libc spawned but before returning
            # a PID is conservatively unknown. Such a failure retains the journal
            # and scope, never triggers automatic cleanup/revocation or PASS.
            self.uncertain_spawn = True
            self.pid = os.posix_spawn(str(image), [str(image)],
                                     {'PATH': '/usr/bin:/bin', 'LANG': 'C'},
                                     file_actions=actions, setsigmask=(),
                                     setsigdef=(signal.SIGPIPE, signal.SIGCHLD,
                                                signal.SIGHUP, signal.SIGTERM,
                                                signal.SIGINT, signal.SIGALRM))
            require(self.pid > 0, 'invalid returned child PID')
            self.uncertain_spawn = False
            self.command, self.reply = cw, rr
            pipes.remove(cw)
            pipes.remove(rr)
            # Close our child-only pipe ends before waiting for its handshake;
            # otherwise our retained writer would hide an actual child EOF.
            for fd in sources+pipes:
                os.close(fd)
            sources.clear()
            pipes.clear()
            signal.pthread_sigmask(signal.SIG_SETMASK, prior_mask)
            prior_mask = None
            os.set_blocking(cw, False)
            os.set_blocking(rr, False)
            send(self.command, config)
            self.context = message(self.reply)
            require(self.context['stage'] in ('context', 'writer-context'),
                    'role context setup')
        finally:
            for fd in sources+pipes:
                os.close(fd)
            if prior_mask is not None:
                signal.pthread_sigmask(signal.SIG_SETMASK, prior_mask)

    def request(self, name, **extra):
        require(self.status is None, 'child already reaped')
        send(self.command, {'command': name, **extra})
        reply = message(self.reply)
        require(reply['stage'] == name, 'unexpected role reply')
        print('ROLE_REPLY='+json.dumps(reply, sort_keys=True), flush=True)
        return reply

    def begin_create(self, descriptor):
        send(self.command, {'command': 'create', 'descriptor': descriptor})

    def receive(self):
        result = message(self.reply)
        print('ROLE_REPLY='+json.dumps(result, sort_keys=True), flush=True)
        return result

    def wait(self, seconds):
        require(self.pid is not None and not self.uncertain_spawn,
                'exclusive child outcome unavailable')
        end = time.monotonic()+seconds
        while self.status is None and time.monotonic() < end:
            try:
                pid, status = os.waitpid(self.pid, os.WNOHANG)
            except ChildProcessError:
                self.uncertain_spawn = True
                raise RuntimeError('exclusive child observation lost')
            if pid:
                require(pid == self.pid, 'wrong direct child')
                self.status = os.waitstatus_to_exitcode(status)
                break
            time.sleep(.01)
        return self.status

    def stop(self):
        # Closing the command pipe delivers loss to the trusted image; its local
        # C handle must close or the process fails. No arbitrary executable exists.
        if self.command >= 0:
            os.close(self.command)
            self.command = -1
        require(not self.uncertain_spawn, 'spawn outcome UNCONFIRMED')
        if self.pid is not None and self.status is None and self.wait(5) is None:
            # Exclusive unreaped direct child, no external SIGCHLD reaper. Marked
            # complete by wait before any later signal. Numeric PID stays reserved.
            try:
                os.kill(self.pid, signal.SIGKILL)
            except ProcessLookupError:
                # ESRCH alone is not proof. Only our actual wait/reap completes
                # this record; ECHILD retains uncertainty and disables signaling.
                pass
            require(self.wait(5) is not None, 'child cleanup UNCONFIRMED')
        if self.reply >= 0:
            os.close(self.reply)
            self.reply = -1

    def finish(self):
        self.request('exit')
        require(self.wait(5) == 0, 'role normal exit missing/nonzero')
        self.stop()
