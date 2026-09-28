# SPDX-License-Identifier: Apache-2.0
"""Fixed-role transport mechanics only: no credentials, SQLite or policy writes."""
import errno
import fcntl
import importlib.util
import json
import os
from pathlib import Path
import signal
import stat
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

source = Path(__file__).parents[1]/'integration/read_policy_roles.py'
spec = importlib.util.spec_from_file_location('read_policy_roles', source)
roles = importlib.util.module_from_spec(spec)
spec.loader.exec_module(roles)

# Deliberately an unprivileged test image. It does NOT exercise the real C++
# credential/context guards; native fixed-role execution is a separate gate.
IMAGE = '''import fcntl,json,os,signal,stat,sys,time
fds=[]
for name in os.listdir('/proc/self/fd'):
 try: os.fstat(int(name)); fds.append(int(name))
 except OSError: pass
assert sorted(fds)==list(range(6)), fds
for fd in range(3):
 s=os.fstat(fd); flags=fcntl.fcntl(fd,fcntl.F_GETFL)
 assert stat.S_ISCHR(s.st_mode) and s.st_rdev==os.makedev(1,3)
 assert flags & os.O_ACCMODE == (os.O_RDONLY if fd==0 else os.O_WRONLY)
a,b=os.fstat(3),os.fstat(4)
assert stat.S_ISFIFO(a.st_mode) and stat.S_ISFIFO(b.st_mode)
assert (a.st_dev,a.st_ino)!=(b.st_dev,b.st_ino)
assert fcntl.fcntl(3,fcntl.F_GETFL)&os.O_ACCMODE==os.O_RDONLY
assert fcntl.fcntl(4,fcntl.F_GETFL)&os.O_ACCMODE==os.O_WRONLY
assert stat.S_ISREG(os.fstat(5).st_mode)
assert not signal.pthread_sigmask(signal.SIG_BLOCK,[])
for fd in range(6): fcntl.fcntl(fd,fcntl.F_SETFD,fcntl.FD_CLOEXEC)
r=os.fdopen(3,'r'); w=os.fdopen(4,'w',buffering=1)
config=json.loads(r.readline())
if config.get('fail'): sys.exit(7)
print(json.dumps({'stage':'context','fd_count':6}),file=w)
if config.get('hold'):
 signal.signal(signal.SIGTERM,signal.SIG_IGN); time.sleep(30); sys.exit(0)
for line in r:
 request=json.loads(line); command=request['command']
 print(json.dumps({'stage':command}),file=w)
 if command=='exit': sys.exit(0)
'''


class Fixture(unittest.TestCase):
    def setUp(self):
        # Executable image lives under the working/build directory, never /tmp
        # (the target can mount /tmp noexec).
        self.temp = tempfile.TemporaryDirectory(prefix='role-test-', dir=Path.cwd())
        self.root = Path(self.temp.name)
        self.image = self.root/'image'
        self.image.write_text('#!'+sys.executable+'\n'+IMAGE)
        self.image.chmod(0o700)
        self.lock = os.open(self.root/'recovery.lock',
                            os.O_RDWR | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC, 0o600)
        os.fchmod(self.lock, 0o600)
        fcntl.flock(self.lock, fcntl.LOCK_SH)
        self.owned = []

    def tearDown(self):
        for role in self.owned:
            if not role.uncertain_spawn:
                role.stop()
        if self.lock >= 0:
            os.close(self.lock)
        self.temp.cleanup()

    def start(self, **config):
        role = roles.Role()
        self.owned.append(role)  # preallocated ownership before any spawn
        role.spawn(self.image, self.lock, config)
        return role


class Transport(Fixture):
    def test_fd_table_high_sentinel_and_normal_exit(self):
        sentinel = fcntl.fcntl(self.lock, fcntl.F_DUPFD, 200)
        try:
            role = self.start()
            self.assertEqual(role.context['fd_count'], 6)
            self.assertEqual(role.request('probe')['stage'], 'probe')
            role.finish()
            self.assertEqual(role.status, 0)
            self.assertEqual((role.command, role.reply), (-1, -1))
        finally:
            os.close(sentinel)

    def test_closed_stdio_in_separate_coordinator(self):
        driver = self.root/'driver.py'
        driver.write_text("""import importlib.util,os,sys
spec=importlib.util.spec_from_file_location('transport',sys.argv[1])
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
fd=os.open(sys.argv[3],os.O_RDWR|os.O_CLOEXEC)
for n in range(3): os.close(n)
r=m.Role();r.spawn(sys.argv[2],fd,{})
m.send(r.command,{'command':'exit'})
assert m.message(r.reply)=={'stage':'exit'}
assert r.wait(5)==0
r.stop();os.close(fd)
assert r.status==0 and r.command==r.reply==-1
# The test deliberately removed Python's stdio underneath its wrappers. Only
# after verified child reap/reference close, avoid shutdown flushing stale FDs.
os._exit(0)
""")
        result = subprocess.run([sys.executable, str(driver), str(source),
                                 str(self.image), str(self.root/'recovery.lock')],
                                timeout=10)
        self.assertEqual(result.returncode, 0)

    def test_handshake_failure_keeps_positive_child_for_reap(self):
        with self.assertRaisesRegex(RuntimeError, 'EOF'):
            self.start(fail=True)
        role = self.owned[-1]
        self.assertGreater(role.pid, 0)
        self.assertFalse(role.uncertain_spawn)
        role.stop()
        self.assertEqual(role.status, 7)

    def test_unknown_spawn_refuses_cleanup_without_signal(self):
        role = roles.Role()
        with patch.object(roles.os, 'posix_spawn', side_effect=MemoryError), \
                self.assertRaises(MemoryError):
            role.spawn(self.image, self.lock, {})
        self.assertTrue(role.uncertain_spawn)
        self.assertIsNone(role.pid)
        with patch.object(roles.os, 'kill') as kill, \
                self.assertRaisesRegex(RuntimeError, 'UNCONFIRMED'):
            role.stop()
        kill.assert_not_called()

    def test_snapshot_and_spawn_block_handlers_then_restore(self):
        original = os.posix_spawn
        before = signal.pthread_sigmask(signal.SIG_BLOCK, [])
        def checked(*args, **kwargs):
            mask = signal.pthread_sigmask(signal.SIG_BLOCK, [])
            self.assertIn(signal.SIGINT, mask)
            self.assertIn(signal.SIGTERM, mask)
            return original(*args, **kwargs)
        with patch.object(roles.os, 'posix_spawn', side_effect=checked):
            self.start().finish()
        self.assertEqual(signal.pthread_sigmask(signal.SIG_BLOCK, []), before)

    def test_inherited_sh_survives_parent_close_until_role_exit(self):
        role = self.start()
        os.close(self.lock)
        self.lock = -1
        independent = os.open(self.root/'recovery.lock', os.O_RDWR | os.O_CLOEXEC)
        try:
            with self.assertRaises(BlockingIOError):
                fcntl.flock(independent, fcntl.LOCK_EX | fcntl.LOCK_NB)
            role.finish()
            fcntl.flock(independent, fcntl.LOCK_EX | fcntl.LOCK_NB)
        finally:
            os.close(independent)

    def test_echild_disables_later_signaling(self):
        role = roles.Role()
        role.pid = 12345  # injected observation only; never a real signal target
        with patch.object(roles.os, 'waitpid', side_effect=ChildProcessError), \
                self.assertRaisesRegex(RuntimeError, 'observation lost'):
            role.wait(.1)
        with patch.object(roles.os, 'kill') as kill, \
                self.assertRaisesRegex(RuntimeError, 'UNCONFIRMED'):
            role.stop()
        kill.assert_not_called()

    def test_esrch_requires_actual_reap(self):
        role = roles.Role()
        role.pid = 12345
        with patch.object(role, 'wait', side_effect=[None, -9]) as wait, \
                patch.object(roles.os, 'kill', side_effect=ProcessLookupError):
            role.stop()
        self.assertEqual(wait.call_count, 2)
        role.status = None
        with patch.object(role, 'wait', side_effect=[None, None]), \
                patch.object(roles.os, 'kill', side_effect=ProcessLookupError), \
                self.assertRaisesRegex(RuntimeError, 'UNCONFIRMED'):
            role.stop()

    def test_reply_eof_duplicate_size_and_absolute_deadline(self):
        for payload, cause in [(b'', 'EOF'), (b'{"a":1,"a":2}\n', 'duplicate'),
                               (b'x'*4096, 'size')]:
            r, w = os.pipe2(os.O_CLOEXEC | os.O_NONBLOCK)
            try:
                os.write(w, payload)
                os.close(w); w = -1
                with self.assertRaisesRegex(RuntimeError, cause):
                    # Byte-limit testing is separate from the partial-frame
                    # deadline below; native one-byte reads can exceed100ms.
                    roles.message(r, 2 if len(payload) == 4096 else .1)
            finally:
                os.close(r)
                if w >= 0: os.close(w)
        r, w = os.pipe2(os.O_CLOEXEC | os.O_NONBLOCK)
        try:
            os.write(w, b'{')
            start = time.monotonic()
            with self.assertRaisesRegex(RuntimeError, 'deadline'):
                roles.message(r, .05)
            self.assertLess(time.monotonic()-start, .5)
        finally:
            os.close(r); os.close(w)

    def test_nonblocking_command_partial_write_and_full_pipe_deadline(self):
        r, w = os.pipe2(os.O_CLOEXEC | os.O_NONBLOCK)
        original = os.write
        try:
            def partial(fd, data):
                return original(fd, data[:3])
            with patch.object(roles.os, 'write', side_effect=partial):
                roles.send(w, {'command':'probe'})
            self.assertEqual(json.loads(os.read(r, 100)), {'command':'probe'})
            while True:
                try: original(w, b'x'*4096)
                except BlockingIOError: break
            with self.assertRaisesRegex(RuntimeError, 'backpressure deadline'):
                roles.send(w, {'command':'stop'}, .05)
            with self.assertRaisesRegex(RuntimeError, 'command size'):
                roles.send(w, {'command':'x'*4096})
        finally:
            os.close(r); os.close(w)

    def test_always_ready_partial_write_cannot_outlive_budget(self):
        ticks = iter(range(50))
        with patch.object(roles.time, 'monotonic', side_effect=lambda: next(ticks)), \
                patch.object(roles.select, 'select', return_value=([], [7], [])), \
                patch.object(roles.os, 'write', return_value=1) as write, \
                self.assertRaisesRegex(RuntimeError, 'backpressure deadline'):
            roles.send(7, {'command':'probe'}, 3)
        self.assertEqual(write.call_count, 1)

    def test_ready_then_eagain_cannot_outlive_budget(self):
        ticks = iter([0, .1, .2, .3, 1.1])
        with patch.object(roles.time, 'monotonic', side_effect=lambda: next(ticks)), \
                patch.object(roles.select, 'select', return_value=([], [7], [])), \
                patch.object(roles.os, 'write', side_effect=BlockingIOError) as write, \
                self.assertRaisesRegex(RuntimeError, 'backpressure deadline'):
            roles.send(7, {'command':'probe'}, 1)
        self.assertEqual(write.call_count, 1)

    def test_deadline_failure_retains_real_child_owner(self):
        role = self.start()
        ticks = iter(range(50))
        with patch.object(roles.time, 'monotonic', side_effect=lambda: next(ticks)), \
                patch.object(roles.select, 'select', return_value=([], [7], [])), \
                patch.object(roles.os, 'write', return_value=1), \
                self.assertRaisesRegex(RuntimeError, 'backpressure deadline'):
            role.request('probe')
        self.assertGreater(role.pid, 0)
        self.assertIsNone(role.status)
        self.assertFalse(role.uncertain_spawn)
        role.stop()
        self.assertEqual(role.status, 0)


class FixedImageGuards(Fixture):
    """Host-only table refusal, deliberately never reaching label/ID setup."""
    def setUp(self):
        super().setUp()
        image = os.environ.get('CAPMGR_READ_POLICY_ROLE_IMAGE')
        if not image:
            self.temp.cleanup()
            os.close(self.lock)
            self.lock = -1
            self.skipTest('fixed image not selected; transport tests still run')
        self.image = Path(image)

    def attempt(self, mutate, cause):
        original = os.posix_spawn
        def changed(*args, **kwargs):
            actions = list(kwargs['file_actions'])
            kwargs['file_actions'] = mutate(actions)
            return original(*args, **kwargs)
        # With a valid table this fails at the fixed role-name guard BEFORE
        # Drop/SQLite. Corrupted tables fail even earlier and cannot report via4.
        with patch.object(roles.os, 'posix_spawn', side_effect=changed), \
                self.assertRaisesRegex(RuntimeError, cause):
            self.start(role='invalid', key='a'*32)
        role = self.owned[-1]
        role.stop()
        self.assertEqual(role.status, 1)

    def test_valid_table_reaches_only_config_refusal(self):
        if os.getuid() != 0 or os.getgid() != 0:
            self.skipTest('real image requires root-owned journal FD; no grant')
        self.attempt(lambda a: a, 'fixed reader role')

    def test_missing_recovery_fd_refuses_before_diagnostics(self):
        self.attempt(lambda a: a+[(os.POSIX_SPAWN_CLOSE, 5)], 'EOF')

    def test_extra_fd_refuses_before_diagnostics(self):
        self.attempt(lambda a: a+[(os.POSIX_SPAWN_OPEN, 200, '/dev/null',
                                  os.O_RDONLY, 0)], 'EOF')

    def test_wrong_direction_and_alias_refuse_before_diagnostics(self):
        self.attempt(lambda a: a+[(os.POSIX_SPAWN_DUP2, 3, 4)], 'EOF')

    def test_same_pipe_correct_directions_still_refused(self):
        def aliased(actions):
            source_fd = next(a[1] for a in actions
                             if a[0] == os.POSIX_SPAWN_DUP2 and a[2] == 3)
            identity = os.fstat(source_fd)
            for item in Path('/proc/self/fd').iterdir():
                fd = int(item.name)
                try:
                    info = os.fstat(fd)
                    flags = fcntl.fcntl(fd, fcntl.F_GETFL)
                except OSError:
                    continue
                if ((info.st_dev, info.st_ino) ==
                        (identity.st_dev, identity.st_ino) and
                        flags & os.O_ACCMODE == os.O_WRONLY):
                    return [(a[0], fd, 4) if a[0] == os.POSIX_SPAWN_DUP2
                            and a[2] == 4 else a for a in actions]
            self.fail('write end of command pipe missing')
        self.attempt(aliased, 'EOF')

    def test_unexpected_regular_status_fd_is_never_written(self):
        unexpected = self.root/'unexpected'
        unexpected.write_bytes(b'unchanged')
        self.attempt(lambda a: a+[(os.POSIX_SPAWN_OPEN, 4, str(unexpected),
                                  os.O_WRONLY, 0)], 'EOF')
        self.assertEqual(unexpected.read_bytes(), b'unchanged')

    def test_task_enumeration_error_refuses_before_config_or_diagnostics(self):
        image = os.environ.get('CAPMGR_READ_POLICY_TASK_FAILURE_IMAGE')
        if not image:
            self.skipTest('task enumeration failure image not selected')
        self.image = Path(image)
        self.attempt(lambda a: a, 'EOF')

    def test_fd_enumeration_error_refuses_before_config_or_diagnostics(self):
        image = os.environ.get('CAPMGR_READ_POLICY_FD_FAILURE_IMAGE')
        if not image:
            self.skipTest('FD enumeration failure image not selected')
        self.image = Path(image)
        self.attempt(lambda a: a, 'EOF')


if __name__ == '__main__':
    unittest.main()
