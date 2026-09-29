# SPDX-License-Identifier: Apache-2.0
"""Build-only inert reference/module survivor fixture. Never runs on import.

Ordinary SIGCHLD closed topology, one thread/exclusive waiter, trusted Python,
image/loader and stable protected root are premises. Reports do not grant signal
or wait authority. No policy/recovery journal/SQL/task-credential operation.
"""
import ctypes
import errno
import fcntl
import hashlib
import json
import os
from pathlib import Path
import signal
import stat
import sys
import time
import types


def check(value, why):
    if not value:
        raise RuntimeError(why)


def trusted(path, mode=None):
    path = Path(path)
    for q in list(reversed(path.parents)) + [path]:
        s = q.lstat()
        check(s.st_uid == s.st_gid == 0 and not s.st_mode & 0o7022,
              'protected root ancestry')
        check(stat.S_ISDIR(s.st_mode) or (q == path and stat.S_ISREG(s.st_mode)
              and s.st_nlink == 1), 'protected type/link')
        check(not any(a.startswith('system.posix_acl_') for a in
                      os.listxattr(q, follow_symlinks=False)), 'protected ACL')
    if mode is not None:
        check(stat.S_IMODE(path.lstat().st_mode) == mode, 'protected exact mode')
    if path.is_file():
        check('security.capability' not in os.listxattr(path, follow_symlinks=False),
              'protected file capability')


def stable(s):
    return (s.st_dev, s.st_ino, s.st_mode, s.st_uid, s.st_gid, s.st_nlink,
            s.st_size, s.st_mtime_ns, s.st_ctime_ns)


def source_module(path, digest):
    path = Path(path)
    check(path.is_absolute() and path == path.resolve(strict=True),
          'canonical executed source path')
    trusted(path)
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC)
    try:
        before = os.fstat(fd)
        check(stat.S_ISREG(before.st_mode) and before.st_nlink == 1 and
              before.st_uid == before.st_gid == 0 and not before.st_mode & 0o7022,
              'pinned source metadata')
        check(0 < before.st_size <= 1048576, 'bounded source')
        data = bytearray()
        while len(data) < before.st_size:
            chunk = os.read(fd, min(65536, before.st_size-len(data)))
            check(chunk, 'complete source read')
            data.extend(chunk)
        check(not os.read(fd, 1) and stable(before) == stable(os.fstat(fd)) ==
              stable(Path(path).lstat()), 'stable source identity/content')
        check(hashlib.sha256(data).hexdigest() == digest, 'exact source digest')
    finally:
        os.close(fd)
    module = types.ModuleType('checked_survivor_dependency')
    module.__file__ = str(path)
    exec(compile(data, str(path), 'exec'), module.__dict__)
    return module


class Scope:
    def __init__(self):
        trusted('/opt/usr')
        buf = ctypes.create_string_buffer(b'/opt/usr/capmgr-reference-survivor-XXXXXX')
        libc = ctypes.CDLL(None)
        libc.mkdtemp.argtypes = [ctypes.c_char_p]
        libc.mkdtemp.restype = ctypes.c_void_p
        check(libc.mkdtemp(buf), 'owned inert scope')
        self.path = Path(os.fsdecode(buf.value))
        self.anchor = -1
        self.reference = None
        self.done = False
        print('OWNED_REFERENCE_SURVIVOR_SCOPE='+str(self.path), flush=True)
        try:
            os.chmod(self.path, 0o700)
            self.anchor = os.open(self.path, os.O_RDONLY | os.O_DIRECTORY |
                                  os.O_CLOEXEC | os.O_NOFOLLOW)
            self.identity = os.fstat(self.anchor)
            self.verify()
        except BaseException:
            print('RETAINED_REFERENCE_SURVIVOR_SCOPE='+str(self.path), flush=True)
            if self.anchor >= 0:
                os.close(self.anchor)
            raise

    def verify(self):
        trusted(self.path, 0o700)
        held, named = os.fstat(self.anchor), self.path.lstat()
        check((held.st_dev, held.st_ino) == (named.st_dev, named.st_ino) ==
              (self.identity.st_dev, self.identity.st_ino), 'pinned inert scope')

    def ex(self, busy):
        self.verify()
        p = self.path/'reference'
        trusted(p, 0o600)
        fd = os.open(p, os.O_RDWR|os.O_NOFOLLOW|os.O_CLOEXEC)
        try:
            held = os.fstat(fd)
            check((held.st_dev, held.st_ino) == self.reference, 'pinned inert reference')
            try:
                fcntl.flock(fd, fcntl.LOCK_EX|fcntl.LOCK_NB)
            except OSError as error:
                check(error.errno in (errno.EAGAIN, errno.EWOULDBLOCK) and busy,
                      'unexpected lock error')
                print('REFERENCE_EX_CONTENDED', flush=True)  # no holder-type/PID claim
                return
            check(not busy, 'unexpected EX available while owned role nonexit')
            print('REFERENCE_EX_AVAILABLE_AFTER_REAP', flush=True)
        finally:
            os.close(fd)  # No SH acquisition/LOCK_UN; supervisor never owns SH.

    def cleanup(self):
        self.verify()
        allowed = {'reference', 'ready-system301-platform',
                   'body-system301-platform', 'stop-system301-platform',
                   'ready-system301-platform.next', 'body-system301-platform.next'}
        names = set(os.listdir(self.path))
        check('reference' in names and names <= allowed, 'complete inert inventory')
        for name in names:
            p = self.path/name
            trusted(p, 0o600)
            if name == 'reference':
                s = p.lstat()
                check((s.st_dev,s.st_ino) == self.reference, 'retained reference identity')
        # All inventory checked before any mutation; root/exclusive stable path premise.
        for name in names:
            os.unlink(name, dir_fd=self.anchor)
        os.rmdir(self.path)
        self.done = True
        print('REMOVED_REFERENCE_SURVIVOR_SCOPE='+str(self.path), flush=True)

    def close(self):
        if not self.done:
            print('RETAINED_REFERENCE_SURVIVOR_SCOPE='+str(self.path), flush=True)
        os.close(self.anchor)


def bounded_json(path):
    trusted(path, 0o600)
    fd = os.open(path, os.O_RDONLY|os.O_CLOEXEC|os.O_NOFOLLOW)
    try:
        s = os.fstat(fd)
        check(0 < s.st_size <= 4096, 'bounded fixture record')
        data = os.read(fd, 4097)
        check(len(data) == s.st_size and stable(s) == stable(os.fstat(fd)) ==
              stable(Path(path).lstat()), 'stable fixture record')
        return data.rstrip(b'\n')
    finally:
        os.close(fd)


def published_record(core, path):
    # Only the atomically published FINAL name is a completion boundary. A .next
    # pathname (empty or partial) never triggers a read or JSON validation.
    budget = core.Deadline(5)
    while True:
        budget.remaining()
        if path.exists():
            result = core.decode(bounded_json(path))
            budget.remaining()  # includes final read/decode processing
            return result
        time.sleep(.005)


def create_record(path, value):
    data = (json.dumps(value, separators=(',',':'))+'\n').encode()
    check(len(data) <= 4096, 'bounded owned record')
    fd = os.open(path, os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW|os.O_CLOEXEC,0o600)
    try:
        check(os.write(fd, data) == len(data), 'complete owned record write')
        os.fsync(fd)
    finally:
        os.close(fd)


def endpoint_path(endpoint):
    return Path('/run/aul/rpcport/.'+endpoint+'::CapabilityManager')


def cleanup_child(record):
    if record.pid is not None and not record.uncertain and record.status is None:
        record.kill_and_wait(3)


class BarrierRecords:
    """Bounded reports, never signal authority; both independent inputs required."""
    def __init__(self, core, mode):
        self.core, self.mode = core, mode
        self.pid = self.server = None
        self.reference = None
        self.pending = []
        self.barrier = None

    def observe(self, origin, data):
        if origin in ('error','server-error'):
            raise self.core.Failure('role stderr/setup failure: '+repr(data))
        if origin in ('role','server') and data.startswith(b'REAL_GATE_'):
            # Fixed bounded diagnostics are printed only, never barrier/authority.
            # Refusal remains fail-closed. Report bounded escaped bytes even
            # when a diagnostic is malformed; do not decode or print controls.
            prefix = data[:256]
            refusal = ('unexpected native diagnostic mode=' + repr(self.mode) +
                       ' origin=' + repr(origin) + ' bytes=' + str(len(data)) +
                       ' prefix=' + repr(prefix) +
                       ' truncated=' + str(len(prefix) != len(data)))
            check((origin == 'role' and self.mode == 'reader' and
                   data.startswith(b'REAL_GATE_CLIENT_REPLY ')) or
                  (origin == 'server' and data.startswith(b'REAL_GATE_BODY=')),
                  refusal)
            print('SURVIVOR_DIAGNOSTIC='+data.decode('utf8'), flush=True)
            return
        record = self.core.decode(data)
        if origin == 'coordinator' and record.get('stage') == 'spawned':
            check(self.pid is None and set(record) == {'stage','pid','server_pid','dev','ino'},
                  'exact single spawn report')
            check(type(record['pid']) is int and record['pid'] > 0 and
                  type(record['dev']) is int and record['dev'] >= 0 and
                  type(record['ino']) is int and record['ino'] > 0, 'spawn report fields')
            server = record['server_pid']
            check((self.mode == 'reader' and type(server) is int and server > 0 and server != record['pid'])
                  or (self.mode == 'server' and server is None), 'fixed server roster')
            self.pid, self.server = record['pid'], server
            self.reference = (record['dev'],record['ino'])
            self.barrier = self.core.Barrier(self.mode, self.pid, self.server)
            for prior in self.pending: self.barrier.observe(*prior)
            self.pending.clear()
            return
        check(origin in ('role','coordinator'), 'unexpected framed output')
        if self.barrier is None:
            check(len(self.pending) < 2, 'bounded pre-report frames')
            self.pending.append((origin,data))
        else:
            self.barrier.observe(origin,data)

    def ready(self):
        return self.barrier is not None and self.barrier.ready()


def prove_survival(ops, ledger):
    """Actual orchestration seam; any error permanently poisons cleanup."""
    try:
        ops.kill_coordinator()  # exact owned record only, successful SIGKILL/reap
        ledger.reap('coordinator', -1, 9)
        ops.adopt_nonexit()     # kernel P_PID, never just reported PID
        ops.ex(True)
        ops.observe_nonexit()
        ops.wait_role_normal()  # actual exact wait/reap0, NOT ACK or EOF
        ledger.reap('role', 0, 0)
        ops.empty()
        ledger.empty('post', errno.ECHILD)
        ops.endpoint_absent()
        ledger.endpoint_absent = True
        ops.ex(False)
        ops.empty()
        ledger.empty('final', errno.ECHILD)
        check(ledger.eligible(), 'sticky survivor cleanup ineligible')
        ops.remove()
    except BaseException:
        ledger.poison()
        raise


def coordinator(core, children, mapper, scope, image, mode, role_pipes, server_pipes, status):
    roles = [children.OwnedChild(), children.OwnedChild()]  # before SH/spawn
    reference = -1
    try:
        reference = os.open(scope.path/'reference', os.O_RDWR|os.O_CREAT|os.O_EXCL|
                            os.O_CLOEXEC|os.O_NOFOLLOW, 0o600)
        fcntl.flock(reference, fcntl.LOCK_SH|fcntl.LOCK_NB)
        held = os.fstat(reference)
        trusted(scope.path/'reference',0o600)
        endpoint = 'd::org.capmgr.referencesurvivor.'+str(os.getpid())+'.system301-platform'
        server = roles[0] if mode == 'reader' else roles[1]
        if mode == 'reader':
            mapper.spawn_module(server,image,'reader-server',scope.path,endpoint,reference,
                                (server_pipes.ends[0],server_pipes.ends[3],server_pipes.ends[5]))
            server_pipes.coordinator_after_positive_spawn()
            ready = scope.path/'ready-system301-platform'
            check(core.exact(published_record(core, ready), {'stage':'ready'}),
                  'real Listen readiness')
        survivor = roles[1]
        mapper.spawn_module(survivor,image,'reader-hold' if mode == 'reader' else 'server-hold',
                            scope.path,endpoint,reference,
                            (role_pipes.ends[0],role_pipes.ends[3],role_pipes.ends[5]))
        role_pipes.coordinator_after_positive_spawn()
        core.send(status, (json.dumps({'stage':'spawned','pid':survivor.pid,
            'server_pid':server.pid if mode == 'reader' else None,
            'dev':held.st_dev,'ino':held.st_ino})+'\n').encode(),core.Deadline(1))
        if mode == 'reader':
            path = scope.path/'body-system301-platform'
            body = published_record(core, path)
            expected = {'stage':'server-cancel-body','role':'system301-platform',
                        'pid':survivor.pid,'uid':301,'gid':301,'socket_label':'System',
                        'token':'system301-platform:'+str(survivor.pid),
                        'cancel_calls':1,'other_calls':0}
            check(core.exact(body,expected), 'actual body/owned PID/token correlation')
            create_record(scope.path/'stop-system301-platform',{'stage':'stop'})
            check(server.wait(5) == 0, 'reader-case server actual normal reap')
            check(not os.path.lexists(endpoint_path(endpoint)), 'reader-case endpoint absence')
            record = {'stage':'reader-correlated','pid':survivor.pid,'server_pid':server.pid,
                      'server_reaped':True,'server_exit':0,'endpoint_absent':True,
                      'reply':-6,'body':body}
        else:
            record = {'stage':'server-ready','pid':survivor.pid,'services':0,'cancel':0,
                      'other':0,'rejected':0,'created':0}
        core.send(status,(json.dumps(record)+'\n').encode(),core.Deadline(1))
        # Keep every spawned child UNREAPED here; only intended SIGKILL ends us.
        # This time is an opportunity, not a liveness premise for the supervisor.
        end = core.Deadline(40)
        while True:
            end.remaining(); time.sleep(.02)
    except BaseException as error:
        print('SURVIVOR_COORDINATOR_FAIL='+repr(error),file=sys.stderr,flush=True)
        for role in roles:
            try: cleanup_child(role)
            except BaseException as cleanup: print('COORDINATOR_CHILD_UNCERTAIN='+repr(cleanup),file=sys.stderr,flush=True)
        return 1
    finally:
        if reference >= 0: os.close(reference)  # own SH reference only; never LOCK_UN


class RealProof:
    def __init__(self, core, children, scope, output, reports, coord, role, ledger):
        self.core = core
        self.children,self.scope,self.output,self.reports = children,scope,output,reports
        self.coord,self.role,self.ledger = coord,role,ledger
        self.endpoint = 'd::org.capmgr.referencesurvivor.'+str(coord.pid)+'.system301-platform'

    def kill_coordinator(self):
        check(self.coord.kill_and_wait(3) == -signal.SIGKILL, 'actual coordinator signalled reap')
        print('OWNED_COORDINATOR_REAP=-9',flush=True)
    def adopt_nonexit(self):
        check(self.role.verify_adoption(self.reports.pid), 'adopted role already exited')
        print('OWNED_ADOPTED_ROLE_NONEXIT=before',flush=True)
    def observe_nonexit(self):
        check(not self.role.observe(), 'owned role exited before causal after-observation')
        print('OWNED_ADOPTED_ROLE_NONEXIT=after',flush=True)
    def ex(self, busy): self.scope.ex(busy)
    def wait_role_normal(self):
        budget = self.core.Deadline(30)
        while not self.role.observe():
            if self.output.live:
                for origin,data in self.output.step(budget):
                    # Independent output pipes can deliver earlier diagnostics
                    # after the barrier. Duplicate/new framed evidence rejects.
                    self.reports.observe(origin, data)
            else:
                budget.remaining(); time.sleep(.005)
        check(self.role.reap() == 0, 'actual adopted normal reap0')
        print('OWNED_ADOPTED_ROLE_REAP=0',flush=True)
        while self.output.live:
            for origin, data in self.output.step(budget):
                self.reports.observe(origin, data)
        budget.remaining()
    def empty(self): self.children.require_no_children()
    def endpoint_absent(self): check(not os.path.lexists(endpoint_path(self.endpoint)), 'retained server endpoint')
    def remove(self): self.scope.cleanup()


def experiment(core, children, mapper, image, mode):
    children.prepare_wait_boundaries()
    children.require_no_children()  # BEFORE scope/fork/SH exists
    coord, role = children.OwnedChild(), children.OwnedChild()
    ledger = core.Retirement()
    reports = BarrierRecords(core, mode)
    scope = Scope()
    role_pipes = server_pipes = None
    status_r = status_w = -1
    try:
        role_pipes = core.PipeEnds()
        server_pipes = core.PipeEnds() if mode == 'reader' else None
        status_r, status_w = os.pipe2(os.O_CLOEXEC | os.O_NONBLOCK)
        mask = signal.pthread_sigmask(signal.SIG_BLOCK,
                                     signal.valid_signals()-{signal.SIGKILL, signal.SIGSTOP})
        try:
            coord.uncertain = True
            pid = os.fork()
            if pid == 0:
                # No Python unwinding through the supervisor's finally blocks.
                try:
                    signal.pthread_sigmask(signal.SIG_SETMASK, mask)
                    role_pipes.coordinator_after_fork()
                    if server_pipes:
                        server_pipes.coordinator_after_fork()
                    os.close(status_r)
                    result = coordinator(core, children, mapper, scope, image,
                                         mode, role_pipes, server_pipes, status_w)
                except BaseException as error:
                    print('COORDINATOR_SETUP_UNCERTAIN='+repr(error),
                          file=sys.stderr, flush=True)
                    result = 1
                os._exit(result)
            coord.attach_direct(pid)  # before logging/handshake/signal unblock
        finally:
            signal.pthread_sigmask(signal.SIG_SETMASK, mask)
        role_pipes.supervisor_after_fork()
        if server_pipes:
            server_pipes.supervisor_after_fork()
        os.close(status_w)
        status_w = -1
        streams = {'role': role_pipes.ends[2], 'error': role_pipes.ends[4],
                   'coordinator': status_r}
        if server_pipes:
            streams.update(server=server_pipes.ends[2],
                           **{'server-error': server_pipes.ends[4]})
        output = core.Output(streams)
        deadline = core.Deadline(15)
        while not reports.ready():
            for origin, data in output.step(deadline):
                reports.observe(origin, data)
            deadline.remaining()  # decoding cannot extend the barrier budget
            check('role' in output.live and 'coordinator' in output.live,
                  'premature role/coordinator EOF is not readiness')
        scope.reference = reports.reference
        print('SURVIVOR_BARRIER_READY='+mode, flush=True)
        role_pipes.supervisor_control_hup()
        if server_pipes:
            server_pipes.supervisor_control_hup()
        prove_survival(RealProof(core, children, scope, output, reports,
                                 coord, role, ledger), ledger)
    except BaseException:
        ledger.poison()
        # Only exact known children may be cleaned up. Never repair a lost proof.
        try:
            cleanup_child(coord)
        except BaseException as error:
            print('COORDINATOR_CLEANUP_UNCERTAIN='+repr(error), flush=True)
        if role.pid is None and reports.pid is not None and coord.status is not None:
            try:
                role.verify_adoption(reports.pid)
            except BaseException as error:
                print('ROLE_ADOPTION_UNCERTAIN='+repr(error), flush=True)
        try:
            cleanup_child(role)
        except BaseException as error:
            print('ROLE_CLEANUP_UNCERTAIN='+repr(error), flush=True)
        raise
    finally:
        # Attempt every owned IPC close, preserving any error. No close supplies
        # child absence or a SH release; scope destruction never deletes it.
        errors = []
        for pipes in (role_pipes, server_pipes):
            if pipes is not None:
                try:
                    pipes.close()
                except BaseException as error:
                    errors.append(error)
        for fd in (status_r, status_w):
            if fd >= 0:
                try:
                    os.close(fd)
                except BaseException as error:
                    errors.append(error)
        try:
            scope.close()
        except BaseException as error:
            errors.append(error)
        if errors:
            ledger.poison()
            raise RuntimeError('owned IPC/scope-close failure: '+repr(errors))
    print('REFERENCE_MODULE_SURVIVOR_CASE_PASS='+mode, flush=True)


if __name__ == '__main__':
    try:
        check(len(sys.argv) == 3 and sys.argv[1] == '--run' and
              os.getuid() == os.geteuid() == os.getgid() == os.getegid() == 0,
              'fixed root opt-in only')
        os.umask(0o077)
        image = Path(sys.argv[2])
        check(image.is_absolute() and image.name == 'capmgr-reference-module-probe' and
              image == image.resolve(strict=True), 'fixed canonical image path')
        trusted(image,0o755)
        source = Path(__file__).resolve(strict=True).parent
        # Fixed source-only imports, pinned at freeze; trusted interpreter/stdlib
        # and no-hostile-root/no-update are separately checked invocation premises.
        core = source_module(source/'read_policy_survivor_core.py','cf1dc5ba64836a022919cb368a829a5fe8292cbd974ee9965bb07f1ca7ec481a')
        children = source_module(source/'read_policy_children.py','24da0320ce6405275f7ed05da62422dfeb997bb23fea802157a3346f86683e13')
        mapper = source_module(source/'read_policy_survivor_spawn.py','44fad389ef4db4dbf13b6a0f2b540e54e6dca9e279140fdc5703917ec1e4b207')
        experiment(core,children,mapper,image,'server')
        experiment(core,children,mapper,image,'reader')
        print('REFERENCE_MODULE_SURVIVOR_FIXTURE_PASS',flush=True)
    except BaseException as error:
        print('REFERENCE_MODULE_SURVIVOR_FIXTURE_FAIL='+repr(error),file=sys.stderr,flush=True)
        sys.exit(1)
