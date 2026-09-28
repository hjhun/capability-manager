# SPDX-License-Identifier: Apache-2.0
"""Private exact-child kernel/failure tests: no root policy, role or SQLite."""
import importlib.util
import errno
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
m.prepare_wait_boundaries()
m.require_no_children()
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
 m.require_no_children()
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


class WaitBoundaries(unittest.TestCase):
    def test_only_exact_echild_proves_empty_and_never_reaps(self):
        with patch.object(children.os, 'waitid',
                          side_effect=ChildProcessError(errno.ECHILD, 'none')) as wait, \
                patch.object(children.os, 'waitpid') as reap, \
                patch.object(children.os, 'kill') as kill:
            children.require_no_children()
        wait.assert_called_once_with(os.P_ALL, 0, os.WEXITED | os.WNOHANG | os.WNOWAIT)
        reap.assert_not_called()
        kill.assert_not_called()

    def test_no_event_and_zombie_event_are_not_absence(self):
        for observed in (None, type('Status', (), {'si_pid':12345})()):
            with self.subTest(observed=observed), \
                    patch.object(children.os, 'waitid', return_value=observed), \
                    patch.object(children.os, 'waitpid') as reap, \
                    patch.object(children.os, 'kill') as kill, \
                    self.assertRaisesRegex(RuntimeError, 'absence not proved'):
                children.require_no_children()
            reap.assert_not_called()
            kill.assert_not_called()

    def test_eintr_and_unknown_errors_do_not_become_empty(self):
        for error in (InterruptedError(errno.EINTR, 'interrupted'),
                      PermissionError(errno.EACCES, 'denied'), ChildProcessError(),
                      OSError(errno.EIO, 'unknown wait error')):
            with self.subTest(error=error), \
                    patch.object(children.os, 'waitid', side_effect=error), \
                    self.assertRaises(OSError):
                children.require_no_children()

    def test_setup_resets_sigchld_before_verified_subreaper(self):
        events = []
        with patch.object(children.os, 'listdir', return_value=['1']), \
                patch.object(children.sysconfig, 'get_config_var', return_value=1), \
                patch.object(children.signal, 'getsignal', return_value=signal.SIG_DFL), \
                patch.object(children.signal, 'signal', side_effect=lambda *args:
                    events.append('reset') or signal.SIG_DFL) as reset, \
                patch.object(children, 'enable_subreaper',
                             side_effect=lambda:events.append('verified-subreaper')):
            children.prepare_wait_boundaries()
        reset.assert_called_once_with(signal.SIGCHLD, signal.SIG_DFL)
        self.assertEqual(events, ['reset','verified-subreaper'])

    def test_setup_failure_does_not_create_child_or_proceed_to_subreaper(self):
        cases = [(['1','2'],1,signal.SIG_DFL,signal.SIG_DFL),
                 (['1'],0,signal.SIG_DFL,signal.SIG_DFL),
                 (['1'],1,signal.SIG_IGN,signal.SIG_DFL),
                 (['1'],1,signal.SIG_DFL,signal.SIG_IGN)]
        for tasks, have_action, disposition, prior in cases:
            with self.subTest(tasks=tasks, have_action=have_action,
                              disposition=disposition, prior=prior), \
                    patch.object(children.os, 'listdir', return_value=tasks), \
                    patch.object(children.sysconfig, 'get_config_var', return_value=have_action), \
                    patch.object(children.signal, 'getsignal', return_value=disposition), \
                    patch.object(children.signal, 'signal', return_value=prior), \
                    patch.object(children, 'enable_subreaper') as subreaper, \
                    patch.object(children.os, 'fork') as fork, \
                    self.assertRaises(RuntimeError):
                children.prepare_wait_boundaries()
            subreaper.assert_not_called()
            fork.assert_not_called()
        with patch.object(children.os, 'listdir', return_value=['1']), \
                patch.object(children.sysconfig, 'get_config_var', return_value=1), \
                patch.object(children.signal, 'getsignal', return_value=signal.SIG_DFL), \
                patch.object(children.signal, 'signal', side_effect=OSError('reset failed')), \
                patch.object(children, 'enable_subreaper') as subreaper, \
                self.assertRaises(OSError):
            children.prepare_wait_boundaries()
        subreaper.assert_not_called()

    def test_actual_setup_live_zombie_extra_children_and_exit_race(self):
        driver = '''import importlib.util,os,signal,sys
s=importlib.util.spec_from_file_location('children',sys.argv[1])
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
m.prepare_wait_boundaries();m.require_no_children()
print('ACTUAL_DEFAULT_SIGNAL_SUBREAPER_SETUP_PASS',flush=True)
records=[]
def absence_rejected():
 try:m.require_no_children()
 except RuntimeError:return
 raise AssertionError('child incorrectly considered absent')
def spawn():
 r,w=os.pipe2(os.O_CLOEXEC)
 record=[m.OwnedChild(),w];records.append(record)
 record[0].uncertain=True
 blocked=signal.valid_signals()-{signal.SIGKILL,signal.SIGSTOP}
 prior=signal.pthread_sigmask(signal.SIG_BLOCK,blocked)
 try:
  pid=os.fork()
  if pid==0:
   signal.pthread_sigmask(signal.SIG_SETMASK,prior);signal.alarm(4)
   os.close(w)
   # Close all earlier parent-side writers; inherited copies would keep the
   # other child alive after parent release and invalidate this test harness.
   for previous in records[:-1]:
    if previous[1]>=0:os.close(previous[1])
   os.read(r,1);os.close(r);os._exit(0)
  record[0].attach_direct(pid)
 finally:signal.pthread_sigmask(signal.SIG_SETMASK,prior)
 os.close(r);return record
def release(record,reap):
 child,w=record
 if w>=0:os.close(w);record[1]=-1
 if reap:assert child.wait(3)==0
 else:
  observed=os.waitid(os.P_PID,child.pid,os.WEXITED|os.WNOWAIT)
  assert observed.si_pid==child.pid
try:
 mode=sys.argv[2];known=spawn()
 assert os.waitid(os.P_ALL,0,os.WEXITED|os.WNOHANG|os.WNOWAIT) is None
 absence_rejected()
 if mode=='zombie':
  release(known,False)
  observed=os.waitid(os.P_ALL,0,os.WEXITED|os.WNOHANG|os.WNOWAIT)
  assert observed.si_pid==known[0].pid;absence_rejected()
 elif mode in ('extra-live','extra-zombie'):
  extra=spawn()
  if mode=='extra-zombie':release(extra,False)
  release(known,True);absence_rejected()
  if mode=='extra-zombie':
   observed=os.waitid(os.P_ALL,0,os.WEXITED|os.WNOHANG|os.WNOWAIT)
   assert observed.si_pid==extra[0].pid
 elif mode=='exit-race':
  os.close(known[1]);known[1]=-1
  # P_PID may observe live or terminal; neither allows P_ALL absence until reap.
  known[0].observe();absence_rejected()
finally:
 # Test-created extras are themselves exact positive-return records. We never
 # reap or signal an arbitrary P_ALL result to manufacture an empty boundary.
 for record in records:
  child=record[0]
  if record[1]>=0:os.close(record[1]);record[1]=-1
  if child.status is None:assert child.kill_and_wait(3) is not None
m.require_no_children()
print('EXACT_RECORD_REAPS_THEN_ECHILD_PASS_'+mode,flush=True)
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'driver.py'
            path.write_text(driver)
            for mode in ('live','zombie','extra-live','extra-zombie','exit-race'):
                with self.subTest(mode=mode):
                    result = subprocess.run(
                        [sys.executable,'-B',str(path),str(source),mode],
                        capture_output=True,text=True,timeout=10,
                        env={'PATH':'/usr/bin:/bin','LANG':'C',
                             'PYTHONDONTWRITEBYTECODE':'1','PYTHONNOUSERSITE':'1'})
                    self.assertEqual(result.returncode,0,result.stderr)
                    self.assertIn('ACTUAL_DEFAULT_SIGNAL_SUBREAPER_SETUP_PASS',result.stdout)
                    self.assertIn('EXACT_RECORD_REAPS_THEN_ECHILD_PASS_'+mode,result.stdout)


if __name__ == '__main__':
    unittest.main()
