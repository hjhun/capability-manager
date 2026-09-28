# SPDX-License-Identifier: Apache-2.0
"""Unprivileged mapping/failure tests; no real Drop/image/policy/recovery CLI."""
import fcntl
import os
from pathlib import Path
import signal
import shutil
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch


def source_module(name, path):
    module = types.ModuleType(name)
    module.__file__ = str(path)
    exec(compile(path.read_bytes(), str(path), 'exec'), module.__dict__)
    return module


root = Path(__file__).parents[1]/'integration'
launch = source_module('reference_spawn', root/'read_policy_reference_spawn.py')
children = source_module('children', root/'read_policy_children.py')

# Fixed test image only; does not validate real root/ACL/label/capability setup.
IMAGE = '''import fcntl,os,signal,stat,sys
fds=[]
for name in os.listdir('/proc/self/fd'):
 try: os.fstat(int(name)); fds.append(int(name))
 except OSError: pass
assert sorted(fds)==[0,1,2,4], fds
assert sys.argv[1]=='--reference-context'
assert sys.argv[2] in ('system301-platform','root-user-shell')
held=os.fstat(4)
assert stat.S_ISREG(held.st_mode) and held.st_nlink==1
assert (held.st_dev,held.st_ino)==tuple(map(int,sys.argv[3:]))
assert not fcntl.fcntl(4,fcntl.F_GETFD)&fcntl.FD_CLOEXEC
assert fcntl.fcntl(4,fcntl.F_GETFL)&os.O_ACCMODE==os.O_RDWR
assert not signal.pthread_sigmask(signal.SIG_BLOCK,[])
fcntl.fcntl(4,fcntl.F_SETFD,fcntl.FD_CLOEXEC)
print('MAPPED_REFERENCE',flush=True)
assert os.read(0,1)==b'x'
# Never close/convert journal4; kernel exit releases this inherited reference.
os._exit(0)
'''


class ReferenceSpawn(unittest.TestCase):
    def setUp(self):
        # Manual cleanup: TemporaryDirectory's destructor could delete a scope
        # after an ownership-uncertain tearDown failure.
        self.path = Path(tempfile.mkdtemp(prefix='reference-map-', dir=Path.cwd()))
        self.image = self.path/'image'
        self.image.write_text('#!'+sys.executable+' -I\n'+IMAGE)
        self.image.chmod(0o700)
        self.lock = os.open(self.path/'lock', os.O_CREAT | os.O_EXCL |
                            os.O_RDWR | os.O_CLOEXEC, 0o600)
        os.fchmod(self.lock, 0o600)
        fcntl.flock(self.lock, fcntl.LOCK_SH)
        self.nested_uncertain = False
        self.input, self.command = os.pipe2(os.O_CLOEXEC)
        self.output = os.open(self.path/'reply', os.O_CREAT | os.O_EXCL |
                              os.O_WRONLY | os.O_CLOEXEC, 0o600)
        self.owned = []

    def tearDown(self):
        # Never delete the linked scope on uncertain/unreaped child ownership.
        for child in self.owned:
            if child.uncertain:
                raise RuntimeError('RETAINED_REFERENCE_SCOPE='+str(self.path))
            if child.pid is not None and child.status is None:
                child.kill_and_wait(3)
        if self.nested_uncertain:
            raise RuntimeError('RETAINED_REFERENCE_SCOPE='+str(self.path))
        for fd in (self.lock, self.input, self.command, self.output):
            if fd >= 0:
                os.close(fd)
        shutil.rmtree(self.path)

    def start(self):
        child = children.OwnedChild()
        self.owned.append(child)
        launch.spawn(child, self.image, 'system301-platform', self.lock,
                     (self.input, self.output, self.output))
        return child

    def test_actual_mapping_closes_slot3_and_high_alias_retains_sh_to_exit(self):
        high = fcntl.fcntl(self.lock, fcntl.F_DUPFD, 200)
        os.set_inheritable(high, True)
        self.assertEqual(fcntl.fcntl(high, fcntl.F_GETFD), 0)
        try:
            child = self.start()
            # stdout is regular and bounded; wait for the fake image's ready
            # line, never confuse it with real context/ACL evidence.
            import time
            end = time.monotonic()+3
            while not (self.path/'reply').read_bytes() and time.monotonic()<end:
                time.sleep(.01)
            self.assertEqual((self.path/'reply').read_bytes(),
                             b'MAPPED_REFERENCE\n')
            self.assertFalse(child.observe())
            os.close(self.lock)
            self.lock = -1
            os.close(high)
            high = -1
            with open(self.path/'lock', 'r+') as independent:
                with self.assertRaises(BlockingIOError):
                    fcntl.flock(independent, fcntl.LOCK_EX | fcntl.LOCK_NB)
                os.write(self.command, b'x')
                self.assertEqual(child.wait(3), 0)
                fcntl.flock(independent, fcntl.LOCK_EX | fcntl.LOCK_NB)
        finally:
            if high >= 0:
                os.close(high)

    def test_omitting_high_alias_close_causes_owned_child_rejection(self):
        high = fcntl.fcntl(self.lock, fcntl.F_DUPFD, 200)
        os.set_inheritable(high, True)
        self.assertEqual(fcntl.fcntl(high, fcntl.F_GETFD), 0)
        real_spawn = os.posix_spawn
        def omit_high(*args, **kwargs):
            actions = kwargs['file_actions']
            removed = (os.POSIX_SPAWN_CLOSE, high)
            self.assertIn(removed, actions)
            kwargs['file_actions'] = [action for action in actions
                                      if action != removed]
            return real_spawn(*args, **kwargs)
        try:
            with patch.object(launch.os, 'posix_spawn', side_effect=omit_high):
                child = self.start()
            self.assertEqual(child.wait(3), 1)
            self.assertIn(b'AssertionError', (self.path/'reply').read_bytes())
        finally:
            os.close(high)

    def test_wrong_direction_stdio_rejects_before_spawn(self):
        child = children.OwnedChild()
        with patch.object(launch.os, 'posix_spawn') as spawn, \
                self.assertRaisesRegex(RuntimeError, 'stdio access'):
            launch.spawn(child, self.image, 'root-user-shell', self.lock,
                         (self.command, self.output, self.output))
        spawn.assert_not_called()
        self.assertFalse(child.uncertain)

    def test_missing_stdio_rejects_before_spawn(self):
        child = children.OwnedChild()
        with patch.object(launch.os, 'posix_spawn') as spawn, \
                self.assertRaisesRegex(RuntimeError, 'stdio missing'):
            launch.spawn(child, self.image, 'root-user-shell', self.lock,
                         (-1, self.output, self.output))
        spawn.assert_not_called()

    def test_stdio_alias_of_journal_rejects_before_spawn(self):
        child = children.OwnedChild()
        with patch.object(launch.os, 'posix_spawn') as spawn, \
                self.assertRaisesRegex(RuntimeError, 'aliases journal'):
            launch.spawn(child, self.image, 'root-user-shell', self.lock,
                         (self.lock, self.output, self.output))
        spawn.assert_not_called()

    def test_unknown_spawn_retains_uncertainty_and_original_reference(self):
        child = children.OwnedChild()  # not added to real cleanup records
        with patch.object(launch.os, 'posix_spawn', side_effect=MemoryError), \
                self.assertRaises(MemoryError):
            launch.spawn(child, self.image, 'root-user-shell', self.lock,
                         (self.input, self.output, self.output))
        self.assertTrue(child.uncertain)
        self.assertIsNone(child.pid)
        self.assertGreaterEqual(fcntl.fcntl(self.lock, fcntl.F_GETFD), 0)
        with open(self.path/'lock', 'r+') as independent:
            with self.assertRaises(BlockingIOError):
                fcntl.flock(independent, fcntl.LOCK_EX | fcntl.LOCK_NB)

    def test_unknown_role_rejects_without_fd_changes(self):
        child = children.OwnedChild()
        with patch.object(launch.os, 'posix_spawn') as spawn, \
                self.assertRaisesRegex(RuntimeError, 'fixed role'):
            launch.spawn(child, self.image, 'arbitrary', self.lock,
                         (self.input, self.output, self.output))
        spawn.assert_not_called()

    def test_closed_parent_stdio_uses_explicit_stable_sources(self):
        self.closed_stdio_driver(False)

    def test_omitting_close3_causes_owned_child_rejection(self):
        self.closed_stdio_driver(True)

    def test_nested_timeout_cannot_delete_scope(self):
        with patch.object(subprocess, 'run',
                          side_effect=subprocess.TimeoutExpired('driver', 8)), \
                self.assertRaises(subprocess.TimeoutExpired):
            self.closed_stdio_driver(False)
        self.assert_nested_scope_retained()

    def test_nested_nonzero_exit_cannot_delete_scope(self):
        result = types.SimpleNamespace(returncode=1, stderr=b'failed driver')
        with patch.object(subprocess, 'run', return_value=result), \
                self.assertRaises(AssertionError):
            self.closed_stdio_driver(False)
        self.assert_nested_scope_retained()

    def assert_nested_scope_retained(self):
        self.assertTrue(self.nested_uncertain)
        with patch.object(shutil, 'rmtree') as remove, \
                self.assertRaisesRegex(RuntimeError, 'RETAINED_REFERENCE_SCOPE'):
            self.tearDown()
        remove.assert_not_called()
        self.assertTrue(self.path.is_dir())
        # TEST-only reset: subprocess.run was replaced above, so no driver or
        # descendant was actually launched. Never clear the real failure latch.
        self.nested_uncertain = False

    def closed_stdio_driver(self, omit3):
        # Isolated coordinator avoids Python shutdown wrappers on closed stdio.
        driver = self.path/'driver.py'
        driver.write_text('''import os,sys,types
def load(name,path):
 m=types.ModuleType(name);m.__file__=path
 exec(compile(open(path,'rb').read(),path,'exec'),m.__dict__);return m
m=load('spawn',sys.argv[1]);c=load('children',sys.argv[2])
lock=os.open(sys.argv[4],os.O_RDWR|os.O_CLOEXEC)
assert lock==3
os.set_inheritable(lock,True)
assert os.get_inheritable(3)  # exec alone cannot remove this sentinel
r,w=os.pipe2(os.O_CLOEXEC);out=os.open(os.devnull,os.O_WRONLY|os.O_CLOEXEC)
for fd in range(3): os.close(fd)
child=c.OwnedChild()
try:
 if sys.argv[5]=='omit3':
  real_spawn=os.posix_spawn
  def fault(*args,**kwargs):
   removed=(os.POSIX_SPAWN_CLOSE,3)
   assert removed in kwargs['file_actions']
   kwargs['file_actions']=[a for a in kwargs['file_actions'] if a!=removed]
   return real_spawn(*args,**kwargs)
  m.os.posix_spawn=fault
 m.spawn(child,sys.argv[3],'root-user-shell',lock,(r,out,out))
 if sys.argv[5]!='omit3':os.write(w,b'x')
 assert child.wait(3)==(1 if sys.argv[5]=='omit3' else 0)
finally:
 if child.pid is not None and child.status is None: child.kill_and_wait(3)
 for fd in (lock,r,w,out):os.close(fd)
os._exit(0)
''')
        # Driver reap/timeout is not grandchild absence. Only the trusted driver's
        # success path, after exact child reap and descriptor closes, clears it.
        self.nested_uncertain = True
        result = subprocess.run([sys.executable, '-I', '-B', str(driver),
                                 str(root/'read_policy_reference_spawn.py'),
                                 str(root/'read_policy_children.py'),
                                 str(self.image), str(self.path/'lock'),
                                 'omit3' if omit3 else 'normal'],
                                timeout=8, capture_output=True,
                                env={'PATH':'/usr/bin:/bin','LANG':'C',
                                     'PYTHONDONTWRITEBYTECODE':'1'})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.nested_uncertain = False


if __name__ == '__main__':
    unittest.main()
