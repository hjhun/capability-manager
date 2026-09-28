# SPDX-License-Identifier: Apache-2.0
"""Private exact-child kernel/failure tests: no root policy, role or SQLite."""
import importlib.util
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

source = Path(__file__).parents[1]/'integration/read_policy_children.py'
spec = importlib.util.spec_from_file_location('read_policy_children', source)
children = importlib.util.module_from_spec(spec)
spec.loader.exec_module(children)


class OwnedChildren(unittest.TestCase):
    def test_lost_observation_disables_signal_and_reap(self):
        child = children.OwnedChild()
        child.attach_direct(12345)  # injected syscall responses, no real signal
        with patch.object(children.os, 'waitid', side_effect=ChildProcessError), \
                self.assertRaises(ChildProcessError):
            child.observe()
        self.assertTrue(child.uncertain)
        self.assertFalse(child.can_signal)
        with patch.object(children.os, 'kill') as kill, \
                self.assertRaisesRegex(RuntimeError, 'uncertain'):
            child.kill_and_wait(.1)
        kill.assert_not_called()

    def test_reported_pid_never_authorizes_signal_before_adoption(self):
        child = children.OwnedChild()
        with patch.object(children.os, 'waitid', side_effect=ChildProcessError), \
                self.assertRaises(ChildProcessError):
            child.verify_adoption(12345)
        self.assertTrue(child.uncertain)
        self.assertFalse(child.can_signal)

    def test_terminal_marker_precedes_reap_even_when_reap_fails(self):
        child = children.OwnedChild()
        child.attach_direct(12345)
        observed = type('Status', (), {'si_pid':12345})()
        with patch.object(children.os, 'waitid', return_value=observed):
            self.assertTrue(child.observe())
        self.assertFalse(child.can_signal)
        def lost(pid, flags):
            self.assertTrue(child.uncertain)
            self.assertFalse(child.can_signal)
            raise ChildProcessError()
        with patch.object(children.os, 'waitpid', side_effect=lost), \
                self.assertRaises(ChildProcessError):
            child.reap()
        self.assertTrue(child.uncertain)
        self.assertIsNone(child.status)

    def test_esrch_is_not_absence_without_actual_reap(self):
        child = children.OwnedChild()
        child.attach_direct(12345)
        with patch.object(child, 'observe', return_value=False), \
                patch.object(children.os, 'kill', side_effect=ProcessLookupError), \
                patch.object(child, 'wait', return_value=None), \
                self.assertRaisesRegex(RuntimeError, 'UNCONFIRMED'):
            child.kill_and_wait(.1)
        self.assertIsNone(child.status)

    def test_actual_subreaper_adoption_pin_then_kill_and_reap(self):
        # Isolate process-global subreaper/SIGCHLD state from the test runner.
        driver = '''import importlib.util,os,signal,sys
s=importlib.util.spec_from_file_location('children',sys.argv[1])
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
m.enable_subreaper()
r,w=os.pipe2(os.O_CLOEXEC)
coordinator=m.OwnedChild()
coordinator.uncertain=True
pid=os.fork()
if pid==0:
 signal.alarm(5)  # bounded lifetime even if the supervisor itself is lost
 os.close(r)
 writer=os.fork()
 if writer==0:
  signal.alarm(5)
  os.close(w)
  while True: signal.pause()
 os.write(w,(str(writer)+'\\n').encode())
 while True: signal.pause()
coordinator.attach_direct(pid)
os.close(w)
writer=None;adopted=None
try:
 data=b''
 while not data.endswith(b'\\n'):
  byte=os.read(r,1);assert byte;data+=byte;assert len(data)<32
 writer=int(data)
 assert coordinator.kill_and_wait(3)==-signal.SIGKILL
 adopted=m.OwnedChild()
 assert adopted.verify_adoption(writer)
 assert adopted.can_signal and not adopted.exited
 assert adopted.kill_and_wait(3)==-signal.SIGKILL
 assert not adopted.can_signal and not adopted.uncertain
 assert adopted.status==-signal.SIGKILL
 print('EXACT_ADOPTED_CHILD_REAP_PASS',flush=True)
finally:
 # Failure cleanup uses only our exact kernel-owned records. A reported writer
 # is adopted/verified only after coordinator absence; no blanket PID kill.
 try:
  if coordinator.status is None: coordinator.kill_and_wait(3)
  if writer is not None and adopted is None:
   adopted=m.OwnedChild();adopted.verify_adoption(writer)
  if adopted is not None and adopted.status is None: adopted.kill_and_wait(3)
 except BaseException:
  print('CHILD_CLEANUP_UNCONFIRMED_NO_PASS',flush=True)
  raise
 finally: os.close(r)
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'driver.py'
            path.write_text(driver)
            result = subprocess.run([sys.executable, '-B', str(path), str(source)],
                                    capture_output=True, text=True, timeout=10,
                                    env={'PATH':'/usr/bin:/bin', 'LANG':'C',
                                         'PYTHONDONTWRITEBYTECODE':'1',
                                         'PYTHONNOUSERSITE':'1'})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('EXACT_ADOPTED_CHILD_REAP_PASS', result.stdout)


if __name__ == '__main__':
    unittest.main()
