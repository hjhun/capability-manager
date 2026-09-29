# SPDX-License-Identifier: Apache-2.0
"""Ordinary fixed fake-image pipe/mapping/SH tests; no root/module/Drop."""
import fcntl
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch


def load(name,path):
    module=types.ModuleType(name);module.__file__=str(path)
    exec(compile(path.read_bytes(),str(path),'exec'),module.__dict__)
    return module


source=Path(__file__).parents[1]/'integration'
mapper=load('survivor_mapper',source/'read_policy_survivor_spawn.py')
children=load('survivor_children',source/'read_policy_children.py')
core=load('survivor_pipes',source/'read_policy_survivor_core.py')
IMAGE='''import fcntl,os,select,stat,sys
fds=[]
for name in os.listdir('/proc/self/fd'):
 try:os.fstat(int(name));fds.append(int(name))
 except OSError:pass
assert sorted(fds)==[0,1,2,4],fds
assert sys.argv[1:3]==['--reference-module','reader-hold']
assert tuple(map(int,sys.argv[5:7]))==(os.fstat(4).st_dev,os.fstat(4).st_ino)
assert stat.S_ISREG(os.fstat(4).st_mode) and os.fstat(4).st_nlink==1
assert not os.get_inheritable(3) if 3 in fds else True
assert os.get_inheritable(4)
for fd in (0,1,2):assert fcntl.fcntl(fd,fcntl.F_GETFL)&os.O_NONBLOCK
os.write(1,b'FAKE_MAPPED\\n')
assert select.select([0],[],[],3)[0]==[0]
assert os.read(0,1)==b'x'
os._exit(0)
'''


class SurvivorMapping(unittest.TestCase):
    def setUp(self):
        signal.signal(signal.SIGCHLD,signal.SIG_DFL)
        self.path=Path(tempfile.mkdtemp(prefix='survivor-map-',dir=Path.cwd()))
        self.image=self.path/'fake-image'
        self.image.write_text('#!'+sys.executable+' -I\n'+IMAGE)
        self.image.chmod(0o700)
        self.lock=os.open(self.path/'reference',os.O_CREAT|os.O_EXCL|os.O_RDWR|os.O_CLOEXEC,0o600)
        os.fchmod(self.lock,0o600)
        fcntl.flock(self.lock,fcntl.LOCK_SH|fcntl.LOCK_NB)
        self.pipes=core.PipeEnds()
        self.records=[]
        self.uncertain=False
        self.nested_uncertain=False

    def tearDown(self):
        try:
            for record in self.records:
                if record.uncertain:self.uncertain=True
                if record.pid is not None and record.status is None and not record.uncertain:
                    record.kill_and_wait(3)
        except BaseException:
            self.uncertain=True
            raise
        if self.uncertain or self.nested_uncertain:
            raise RuntimeError('RETAINED_SURVIVOR_MAPPING_SCOPE='+str(self.path))
        self.pipes.close()
        if self.lock>=0:os.close(self.lock)
        shutil.rmtree(self.path)

    def start(self):
        record=children.OwnedChild();self.records.append(record)
        mapper.spawn_module(record,self.image,'reader-hold',self.path,'fixed',self.lock,
                            tuple(self.pipes.ends[n] for n in (0,3,5)))
        return record

    def wait_known(self,child):
        try:
            status=child.wait(3)
            if status is None:raise RuntimeError('owned mapping wait deadline')
            return status
        except BaseException:
            self.uncertain=True
            raise

    def test_real_exec_exact_table_nonblocking_and_child_sh_after_parent_close(self):
        high=fcntl.fcntl(self.lock,fcntl.F_DUPFD,200)
        os.set_inheritable(high,True)
        self.assertTrue(os.get_inheritable(high))
        try:
            child=self.start()
            self.pipes.close_indices((0,3,5))
            output=core.Output({'role':self.pipes.ends[2],'error':self.pipes.ends[4]})
            self.assertEqual(output.step(core.Deadline(3)),[('role',b'FAKE_MAPPED')])
            self.assertFalse(child.observe())
            os.close(self.lock);self.lock=-1
            os.close(high);high=-1
            with open(self.path/'reference','r+') as independent:
                with self.assertRaises(BlockingIOError):
                    fcntl.flock(independent,fcntl.LOCK_EX|fcntl.LOCK_NB)
                core.send(self.pipes.ends[1],b'x',core.Deadline(1))
                self.assertEqual(self.wait_known(child),0)
                fcntl.flock(independent,fcntl.LOCK_EX|fcntl.LOCK_NB)
        finally:
            if high>=0:os.close(high)

    def test_omitting_inheritable_high_close_rejects_and_exact_child_reaped(self):
        high=fcntl.fcntl(self.lock,fcntl.F_DUPFD,200)
        os.set_inheritable(high,True)
        original=os.posix_spawn
        def fault(*args,**kwargs):
            removed=(os.POSIX_SPAWN_CLOSE,high)
            self.assertIn(removed,kwargs['file_actions'])
            kwargs['file_actions']=[v for v in kwargs['file_actions'] if v!=removed]
            return original(*args,**kwargs)
        try:
            with patch.object(mapper.os,'posix_spawn',side_effect=fault):child=self.start()
            self.assertEqual(self.wait_known(child),1)
        finally:os.close(high)

    def test_omitting_explicit_close3_with_inheritable_slot_rejects(self):
        self.slot3_driver()

    def slot3_driver(self):
        # close_fds gives the isolated minimal interpreter its own FD3 slot;
        # never close an unrelated descriptor in the parent test interpreter.
        driver=self.path/'driver.py'
        driver.write_text("""import os,sys,types

def load(name,path):
 m=types.ModuleType(name);m.__file__=path
 exec(compile(open(path,'rb').read(),path,'exec'),m.__dict__);return m
mapper=load('mapper',sys.argv[1]);children=load('children',sys.argv[2]);core=load('core',sys.argv[3])
lock=os.open(sys.argv[5],os.O_RDWR|os.O_CLOEXEC)
assert lock==3
os.set_inheritable(lock,True)
assert os.get_inheritable(3)
pipes=core.PipeEnds();child=children.OwnedChild()
original=os.posix_spawn

def fault(*args,**kwargs):
 removed=(os.POSIX_SPAWN_CLOSE,3)
 assert removed in kwargs['file_actions']
 kwargs['file_actions']=[v for v in kwargs['file_actions'] if v!=removed]
 return original(*args,**kwargs)
mapper.os.posix_spawn=fault
try:
 mapper.spawn_module(child,sys.argv[4],'reader-hold','fixed','fixed',lock,tuple(pipes.ends[n] for n in (0,3,5)))
 # The rejected child never reads control. No scheduling-sensitive pipe write.
 assert child.wait(3)==1
finally:
 if child.pid is not None and child.status is None:child.kill_and_wait(3)
 pipes.close();os.close(lock)
# Success only after exact child reap and all owned closes; no finally bypass.
os._exit(0)
""")
        self.nested_uncertain=True
        result=subprocess.run([sys.executable,'-I','-B',str(driver),
            str(source/'read_policy_survivor_spawn.py'),
            str(source/'read_policy_children.py'),str(source/'read_policy_survivor_core.py'),
            str(self.image),str(self.path/'reference')],timeout=8,capture_output=True,
            close_fds=True,env={'PATH':'/usr/bin:/bin','LANG':'C','PYTHONDONTWRITEBYTECODE':'1'})
        self.assertEqual(result.returncode,0,result.stderr)
        self.nested_uncertain=False

    def test_known_wait_timeout_poison_prevents_scope_removal(self):
        fake=types.SimpleNamespace(wait=lambda seconds:None)
        with self.assertRaisesRegex(RuntimeError,'deadline'):
            self.wait_known(fake)
        with patch.object(shutil,'rmtree') as remove,self.assertRaisesRegex(RuntimeError,'RETAINED_SURVIVOR_MAPPING_SCOPE'):
            self.tearDown()
        remove.assert_not_called()
        self.assertTrue(self.path.is_dir())
        # Injected no-child wait only; never clear this latch after a real timeout.
        self.uncertain=False

    def test_nested_timeout_keeps_scope_despite_driver_reap(self):
        with patch.object(subprocess,'run',side_effect=subprocess.TimeoutExpired('driver',8)),self.assertRaises(subprocess.TimeoutExpired):
            self.slot3_driver()
        self.assert_nested_retained()

    def test_nested_nonzero_keeps_scope(self):
        result=types.SimpleNamespace(returncode=1,stderr=b'injected driver')
        with patch.object(subprocess,'run',return_value=result),self.assertRaises(AssertionError):
            self.slot3_driver()
        self.assert_nested_retained()

    def assert_nested_retained(self):
        with patch.object(shutil,'rmtree') as remove,self.assertRaisesRegex(RuntimeError,'RETAINED_SURVIVOR_MAPPING_SCOPE'):
            self.tearDown()
        remove.assert_not_called()
        self.assertTrue(self.path.is_dir())
        # TEST-only reset after mocked subprocess: no driver/descendant launched.
        self.nested_uncertain=False

    def test_wrong_direction_and_blocking_stdio_refuse_before_spawn(self):
        for stdio in [tuple(self.pipes.ends[n] for n in (1,3,5)),
                      tuple(self.pipes.ends[n] for n in (0,3,5))]:
            if stdio[0]==self.pipes.ends[0]:os.set_blocking(stdio[0],True)
            record=children.OwnedChild()
            with patch.object(mapper.os,'posix_spawn') as spawn,self.assertRaisesRegex(RuntimeError,'stdio access'):
                mapper.spawn_module(record,self.image,'reader-hold',self.path,'fixed',self.lock,stdio)
            spawn.assert_not_called()
            self.assertFalse(record.uncertain)

    def test_unknown_spawn_retains_record_and_reference(self):
        record=children.OwnedChild()  # injected no-launch case, not a real lost child
        with patch.object(mapper.os,'posix_spawn',side_effect=MemoryError),self.assertRaises(MemoryError):
            mapper.spawn_module(record,self.image,'reader-hold',self.path,'fixed',self.lock,
                                tuple(self.pipes.ends[n] for n in (0,3,5)))
        self.assertTrue(record.uncertain)
        self.assertIsNone(record.pid)
        with open(self.path/'reference','r+') as independent:
            with self.assertRaises(BlockingIOError):fcntl.flock(independent,fcntl.LOCK_EX|fcntl.LOCK_NB)


if __name__=='__main__':unittest.main()
