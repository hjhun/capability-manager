#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Explicit root policy fixture; requires exact pre-execution safety review.

No operation runs on import. Fixed image derives from protected build scope.
This is modeled C handoff/root writer evidence, not production provisioning.
"""
import argparse
import errno
import hashlib
import json
import os
from pathlib import Path
import signal
import stat
import subprocess
import sys
import time
import uuid


def require(okay, cause):
    if not okay:
        raise RuntimeError(cause)


def identity(info):
    return [info.st_dev, info.st_ino]


def no_acl(path):
    for name in ('system.posix_acl_access', 'system.posix_acl_default'):
        try:
            os.getxattr(path, name) if isinstance(path, int) else os.getxattr(path, name, follow_symlinks=False)
        except OSError as error:
            require(error.errno in (errno.ENODATA, errno.EOPNOTSUPP), 'ACL lookup error')
        else:
            raise RuntimeError('ACL present')


def trusted_source(source):
    """Explicit root CLI uses only image/protected owned Python source paths."""
    source = Path(source)
    require(source.is_absolute(), 'source must be absolute')
    for ancestor in reversed(source.parents):
        info = ancestor.lstat()
        require(stat.S_ISDIR(info.st_mode) and info.st_uid == 0 and
                not (info.st_mode & 0o7022), 'unsafe source ancestor')
        no_acl(ancestor)
    info = source.lstat()
    require(stat.S_ISREG(info.st_mode) and info.st_uid == 0 and
            info.st_nlink == 1 and not (info.st_mode & 0o7022),
            'unsafe recovery source')
    no_acl(source)


def source_module(source):
    """Compile only validated source bytes, never any bytecode cache.

    Interpreter/stdlib/environment are trusted invocation premises. This loading
    path does not claim to validate those or an arbitrary imported dependency.
    """
    import types
    source = Path(source)
    trusted_source(source)
    before = source.lstat()
    fd = os.open(source, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC)
    try:
        held = os.fstat(fd)
        require(identity(before) == identity(held) and
                stat.S_ISREG(held.st_mode) and held.st_uid == 0 and
                held.st_nlink == 1 and not (held.st_mode & 0o7022),
                'source identity/type changed')
        no_acl(fd)
        require(0 < held.st_size <= 1048576, 'source size limit')
        data = bytearray()
        while len(data) <= 1048576:
            chunk = os.read(fd, 4096)
            if not chunk:
                break
            data.extend(chunk)
        after = os.fstat(fd)
        named = source.lstat()
        require(len(data) == held.st_size and identity(named) == identity(held)
                and after.st_size == held.st_size and
                after.st_mtime_ns == held.st_mtime_ns and
                after.st_ctime_ns == held.st_ctime_ns, 'source changed during load')
    finally:
        os.close(fd)
    module = types.ModuleType('capmgr_read_policy_trusted_parent')
    module.__file__ = str(source)
    module.__package__ = ''
    exec(compile(bytes(data), str(source), 'exec'), module.__dict__)
    return module



def observed(path, uid, gid, mode, label, directory=False):
    info = path.lstat()
    recovery.exact(info, uid, mode, directory=directory)
    require(info.st_gid == gid, 'fixture group mismatch')
    recovery.no_acl(path)
    actual = os.getxattr(path, 'security.SMACK64').rstrip(b'\0').decode()
    require(actual == label, 'fixture label mismatch')
    print('OBSERVED_OBJECT='+json.dumps({'path': str(path), 'uid': uid, 'gid': gid,
                                       'mode': oct(mode), 'label': actual,
                                       'identity': recovery.identity(info)}), flush=True)
    return recovery.identity(info)


def provision(scope, key):
    labels = recovery.label_names(key)
    os.setxattr(scope, 'security.SMACK64', labels['Traversal'].encode())
    catalog, generation = scope/'catalog', scope/'generation'
    catalog.mkdir(mode=0o750)
    os.chown(catalog, 0, 10212)
    catalog.chmod(0o2750)
    os.setxattr(catalog, 'security.SMACK64', labels['Catalog'].encode())
    os.setxattr(catalog, 'security.SMACK64TRANSMUTE', b'TRUE')
    generation.mkdir(mode=0o755)
    os.setxattr(generation, 'security.SMACK64', labels['Traversal'].encode())
    lock = generation/'generation.lock'
    fd = os.open(lock, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC, 0o640)
    try:
        os.fchown(fd, 0, 10212)
        os.fchmod(fd, 0o640)
        os.setxattr(fd, 'security.SMACK64', labels['Lock'].encode())
    finally:
        os.close(fd)
    observed(scope, 0, 0, 0o755, labels['Traversal'], True)
    observed(generation, 0, 0, 0o755, labels['Traversal'], True)
    observed(catalog, 0, 10212, 0o2750, labels['Catalog'], True)
    observed(lock, 0, 10212, 0o640, labels['Lock'])
    return catalog


def observe_data(catalog, key):
    return [observed(catalog/name, 0, 10212, 0o640,
                     recovery.label_names(key)['Catalog'])
            for name in ('catalog.db', 'catalog.db-wal', 'catalog.db-shm')]


def dependencies():
    global recovery, transport, children
    here = Path(__file__).absolute().parent
    trusted_source(Path(__file__).absolute())
    # No SourceFileLoader or unchecked cache for either project dependency.
    recovery = source_module(here/'read_policy_recovery.py')
    transport = source_module(here/'read_policy_roles.py')
    children = source_module(here/'read_policy_children.py')
    recovery.Operations().trusted_parent()


def context(reply, key, role, writer_label):
    if role == 'writer':
        require(reply == {'stage':'writer-context','label':writer_label,
                          'scope':'root-writer-only'}, 'root writer context mismatch')
        return
    labels = recovery.label_names(key)
    require(reply == {'stage':'context','role':role,'uid':301,'gid':301,
                      'platform_group':role != 'denied-dac',
                      'caps':'all-zero','NNP':1,
                      'label':labels['Denied' if role == 'denied-mac' else 'Allowed']},
            'full fixed reader context mismatch')


def executable(path):
    trusted_source(path)
    info = path.lstat()
    require(info.st_gid == 0 and info.st_mode & 0o7777 == 0o755,
            'fixed executable mode/group')
    try:
        os.getxattr(path, 'security.capability', follow_symlinks=False)
    except OSError as error:
        require(error.errno in (errno.ENODATA, errno.EOPNOTSUPP),
                'executable capability lookup failed')
    else:
        raise RuntimeError('executable file capabilities')
    print('FIXED_EXECUTABLE='+str(path)+' SHA='+hashlib.sha256(path.read_bytes()).hexdigest(), flush=True)


def model_create(writer, reader, slot):
    grant = writer.request('issue', slot=slot)
    require(grant['slot'] == slot and grant['transport'] == 'modeled identity; no nonce or TIDL',
            'modeled issuer receipt')
    reader.begin_create(grant['descriptor'])
    reply = reader.receive()
    if reader.context['role'] == 'allowed':
        require(reply == {'stage':'confirm'}, 'modeled confirmation needed')
        # The issuer still owns its independent lease while local C validation
        # has acquired another one and asks for modeled same-instance confirmation.
        transport.send(reader.command, {'command':'confirmed'})
        reply = reader.receive()
        require(reply == {'stage':'create','code':0,'handle':True,'Authorize':1,
                          'Confirm':1,'Finish':1,'lease_admission_returned':True,
                          'SQLite':'opened-and-validated','transport':'explicitly modeled'},
                'allowed modeled C create')
    else:
        require(reply['stage']=='create' and reply['handle'] is False and
                reply['Authorize']==1 and reply['Confirm']==reply['Finish']==0 and
                reply['transport']=='explicitly modeled' and
                reply['lease_admission_returned'] is False and reply['SQLite']=='NOT_RUN',
                'denied modeled C create before client SQLite construction')
    writer.request('release', slot=slot)
    return reply


def query(reader, generation):
    reply = reader.request('query')
    require(reply['query_IPC'] is False and reply['detail']['desc']=='generation'+str(generation),
            'local query generation mismatch')


def journal_cli(journal, assertion=False):
    script = Path(__file__).absolute().with_name('read_policy_recovery.py')
    python = Path(sys.executable).resolve()
    trusted_source(script)
    executable(python)
    flag = '--assert-contended' if assertion else '--recover'
    args = [str(python),'-B',str(script),flag,str(journal)]
    print('REAL_JOURNAL_ARGV='+json.dumps(args),flush=True)
    result = subprocess.run(args,cwd=script.parent,
                            env={'PATH':'/usr/bin:/bin','LANG':'C',
                                 'PYTHONNOUSERSITE':'1',
                                 'PYTHONDONTWRITEBYTECODE':'1'},
                            stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                            timeout=8 if assertion else 30)
    require(len(result.stdout)<=32768,'recovery output limit')
    text = result.stdout.decode()
    print(text,end='',flush=True)
    require(result.returncode==0,'real journal CLI failed')
    prefix = 'READ_POLICY_LOCK_ASSERTION=' if assertion else 'READ_POLICY_RECOVERY='
    rows=[line[len(prefix):] for line in text.splitlines() if line.startswith(prefix)]
    require(len(rows)==1,'journal CLI missing/duplicate result')
    expected = ({'assertion':'EX_CONTENDED','mutations':0} if assertion else
                {'cleanup':'PASS','errors':[],'remaining_rules':[]})
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, 'duplicate journal CLI result key')
            result[key] = value
        return result
    require(json.loads(rows[0],object_pairs_hook=unique)==expected,
            'journal CLI result mismatch')


def real_recovery(journal):
    journal_cli(journal)


def run_matrix():
    dependencies()
    require(os.getresuid()==(0,0,0) and os.getresgid()==(0,0,0),'root fixture IDs')
    require(len(list(Path('/proc/self/task').iterdir()))==1,'single-thread coordinator')
    require(signal.getsignal(signal.SIGCHLD)==signal.SIG_DFL,'exclusive child ownership')
    # Fixed build-only image; no CLI/env-selected image, UID or executable.
    image = Path(__file__).absolute().parents[3]/'build/capmgr-read-policy-role'
    executable(image)
    executable(Path(sys.executable).resolve())
    recovery.trusted_source(Path(__file__).absolute().with_name('read_policy_recovery.py'))
    key = uuid.uuid4().hex
    scope = recovery.PARENT/(recovery.PREFIX+key)
    scope.mkdir(mode=0o755)
    scope.chmod(0o755)  # own new scope, independent of inherited umask
    owner = None
    roles = [transport.Role() for _ in range(6)]
    writer = roles[0]
    writer_label = transport.task_label()
    errors = []
    passed = False
    try:
        owner = recovery.JournalOwner(key,writer_label,scope)
        print('OWNED_SCOPE='+str(scope)+' JOURNAL='+str(owner.path),flush=True)
        python = Path(sys.executable).resolve()
        recovery_command = [str(python),str(Path(__file__).absolute().with_name('read_policy_recovery.py')),
                            '--recover',str(owner.path)]
        owner.install_rules(recovery_command)
        catalog = provision(scope,key)
        # Deliberately DAC-writable MAC probes; observe exact label/mode/group.
        file = catalog/'mac-write-probe'
        fd = os.open(file,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_CLOEXEC,0o666)
        try:
            os.fchmod(fd,0o666)
        finally:
            os.close(fd)
        directory = catalog/'mac-directory'
        directory.mkdir(mode=0o777);directory.chmod(0o777)
        labels = recovery.label_names(key)
        observed(file,0,10212,0o666,labels['Catalog'])
        observed(directory,0,10212,0o777,labels['Catalog'],True)
        # Every fixed child starts from a parent owning zero SQLite/data/lease
        # endpoints. The root writer will create SQLite only in its own process.
        for record,role in zip(roles[:5],['writer','allowed','allowed','denied-mac','denied-dac']):
            record.spawn(image,owner.lock_fd,{'role':role,'key':key})
            context(record.context,key,role,writer_label)
        writer.request('bootstrap')
        identities = observe_data(catalog,key)
        for reader in roles[1:5]:
            reader.request('probe')
        for slot,reader in enumerate(roles[1:5]):
            model_create(writer,reader,slot)
        roles[3].finish();roles[4].finish()
        query(roles[1],1);query(roles[2],1)
        require(writer.request('exclusive')['code']!=0,'two readers must exclude EX')
        writer.request('open');writer.request('commit')
        query(roles[1],2);query(roles[2],2)
        writer.request('close')
        require(observe_data(catalog,key)==identities,'persistent sidecar inode change')
        fresh = roles[5]
        fresh.spawn(image,owner.lock_fd,{'role':'allowed','key':key})
        context(fresh.context,key,'allowed',writer_label)
        fresh.request('probe');model_create(writer,fresh,0);query(fresh,2)
        for reader in roles[1:3]:
            reader.request('destroy');reader.finish()
        require(writer.request('exclusive')['code']!=0,'fresh reader must exclude EX')
        fresh.request('destroy');fresh.finish()
        require(writer.request('exclusive')['code']==0,'physical last close must admit EX')
        writer.finish()
        passed = True
    except BaseException as error:
        errors.append('matrix: '+repr(error))
    finally:
        for record in roles:
            try:
                record.stop()
            except BaseException as error:
                errors.append('child cleanup: '+repr(error))
        if owner is not None:
            try:
                owner.close()
            except BaseException as error:
                errors.append('journal close: '+repr(error))
        # No unknown spawn/outcome or surviving child permits automatic mutation.
        confirmed = all(not r.uncertain_spawn and (r.pid is None or r.status is not None)
                        for r in roles)
        if owner is not None and confirmed:
            try:
                real_recovery(owner.path)
                real_recovery(owner.path)
                require(not scope.exists() and not scope.is_symlink(),'removed scope missing')
                print('REMOVED_SCOPE='+str(scope),flush=True)
            except BaseException as error:
                errors.append('recovery: '+repr(error))
        else:
            errors.append('unconfirmed lifetime or missing durable journal; manual recovery required')
        if errors:
            print('READ_POLICY_FAIL='+json.dumps(errors),flush=True)
            print('RETAINED_OR_RECEIPT_JOURNAL='+str(owner.path if owner else scope),flush=True)
        require(passed and not errors,'matrix/cleanup did not pass')
    print('READ_POLICY_MODELED_MATRIX_PASS_ROOT_WRITER_ONLY',flush=True)


# No automatic/default run. Full main is added only with the frozen complete
# normal/crash supervisor and explicit pre-policy review request.


def own_child_roster():
    # Own fixture child inventory only, not untrusted peer credentials or signal
    # authority. waitid of the exact adopted PID remains the kernel proof.
    require(len(list(Path('/proc/self/task').iterdir())) == 1,
            'single-thread fixture supervisor')
    data = Path('/proc/self/task/'+str(os.getpid())+'/children').read_bytes()
    require(len(data) <= 4096, 'own child inventory limit')
    parts = data.split()
    require(all(x.isdigit() and int(x) > 0 for x in parts) and
            len(set(parts)) == len(parts), 'own child inventory malformed')
    return {int(x) for x in parts}


def crash_coordinator(command, status, key, image):
    """Fixed child creates journal after fork; supervisor inherits no SH."""
    scope = recovery.PARENT/(recovery.PREFIX+key)
    owner = None
    writer = transport.Role()  # retained BEFORE positive/uncertain spawn
    try:
        scope.mkdir(mode=0o755)
        scope.chmod(0o755)
        writer_label = transport.task_label()
        owner = recovery.JournalOwner(key, writer_label, scope)
        args = [str(Path(sys.executable).resolve()),
                str(Path(__file__).absolute().with_name('read_policy_recovery.py')),
                '--recover', str(owner.path)]
        require(owner.ready, 'crash plan not durable')
        print('RECOVERY_ARGV='+json.dumps(args), flush=True)
        transport.send(status, {'stage':'journal-ready','key':key,
                               'journal':str(owner.path),'scope':str(scope)})
        writer.spawn(image, owner.lock_fd, {'role':'writer','key':key})
        context(writer.context, key, 'writer', writer_label)
        held = writer.request('hold-reference')
        require(held == {'stage':'hold-reference','seconds':20,
                        'authority':'reference lifetime only'}, 'hold acknowledgement')
        transport.send(status, {'stage':'held','pid':writer.pid})
        # Exactly the first checked write/flush/close from the durable full plan.
        # The supervisor's barrier prevents any second row in this experiment.
        owner.ops.write_rule(*owner.rules[0])
        transport.send(status, {'stage':'first-write-complete','rows':1})
        transport.message(command, 20)
        raise RuntimeError('unexpected crash barrier release')
    except BaseException as error:
        try:
            transport.send(status, {'stage':'FAIL','cause':repr(error),
                                   'uncertain_spawn':writer.uncertain_spawn})
        except BaseException:
            pass
        # Do not recover/revoke in this process. If supervisor loss makes child
        # absence uncertain, the durable journal stays available independently.
        try:
            writer.stop()
        finally:
            if owner is not None:
                owner.close()
        return 1


def receipt_snapshot(journal):
    result = {}
    for name in ('plan.json', 'recovery.json', 'recovery.json.next'):
        path = journal/name
        try:
            data = path.read_bytes()
        except FileNotFoundError:
            result[name] = None
        else:
            require(len(data) <= 32768, 'receipt snapshot size')
            result[name] = data
    return result


class RecoveryEligibility:
    """Sticky ownership proof loss; later empty inventories cannot restore it."""
    def __init__(self):
        self.eligible = True

    def inventory(self, expected, cause):
        try:
            observed = own_child_roster()
        except BaseException:
            self.eligible = False
            raise
        if observed != expected:
            self.eligible = False
            raise RuntimeError(cause)

    def record_uncertainty(self, *records):
        if any(record.uncertain for record in records):
            self.eligible = False


def run_crash():
    dependencies()
    require(os.getresuid()==(0,0,0) and os.getresgid()==(0,0,0), 'root fixture IDs')
    ownership = RecoveryEligibility()
    ownership.inventory(set(), 'unexpected initial fixture child')
    children.enable_subreaper()
    image = Path(__file__).absolute().parents[3]/'build/capmgr-read-policy-role'
    executable(image)
    executable(Path(sys.executable).resolve())
    key = uuid.uuid4().hex
    expected_journal = recovery.PARENT/(recovery.JOURNAL_PREFIX+key)
    expected_scope = recovery.PARENT/(recovery.PREFIX+key)
    cr, cw = os.pipe2(os.O_CLOEXEC)
    rr, rw = os.pipe2(os.O_CLOEXEC)
    coordinator = children.OwnedChild()
    adopted = children.OwnedChild()
    reported_pid = None
    journal = None
    passed = False
    errors = []
    # No raising handler may interrupt fork return/record assignment. Child
    # restores its original mask before setup; SIGKILL still causes abrupt loss.
    blocked = signal.valid_signals()-{signal.SIGKILL, signal.SIGSTOP}
    prior = signal.pthread_sigmask(signal.SIG_BLOCK, blocked)
    coordinator.uncertain = True
    try:
        pid = os.fork()
        if pid == 0:
            signal.pthread_sigmask(signal.SIG_SETMASK, prior)
            os.close(cw); os.close(rr)
            os.set_blocking(cr, False); os.set_blocking(rw, False)
            code = crash_coordinator(cr, rw, key, image)
            os.close(cr); os.close(rw)
            os._exit(code)  # failure only; successful coordinator is SIGKILLed
        coordinator.attach_direct(pid)
    finally:
        signal.pthread_sigmask(signal.SIG_SETMASK, prior)
    os.close(cr); os.close(rw)
    os.set_blocking(cw, False); os.set_blocking(rr, False)
    try:
        ready = transport.message(rr, 10)
        require(ready == {'stage':'journal-ready','key':key,
                         'journal':str(expected_journal),'scope':str(expected_scope)},
                'crash durable-plan receipt')
        journal = expected_journal
        held = transport.message(rr, 10)
        require(set(held)=={'stage','pid'} and held['stage']=='held' and
                type(held['pid']) is int and held['pid']>1 and
                held['pid'] != coordinator.pid, 'reported hold child')
        reported_pid = held['pid']  # report alone never enables signal
        require(transport.message(rr, 5) == {'stage':'first-write-complete','rows':1},
                'first actual load2 write barrier')
        require(coordinator.kill_and_wait(5) == -signal.SIGKILL,
                'coordinator signalled exit not confirmed')
        ownership.inventory({reported_pid}, 'unexpected adopted descendants')
        require(adopted.verify_adoption(reported_pid), 'hold already exited before assertion')
        before = receipt_snapshot(journal)
        scope_before = identity(expected_scope.lstat())
        journal_cli(journal, assertion=True)
        require(not adopted.observe(), 'hold expired during assertion experiment')
        ownership.inventory({reported_pid}, 'unexpected holder/child inventory')
        require(receipt_snapshot(journal) == before and
                identity(expected_scope.lstat()) == scope_before,
                'assertion changed scope or receipts')
        print('CAUSAL_EX_CONTENDED_WITH_SEPARATE_OWNED_LIVE_REFERENCE_PROOF', flush=True)
        require(adopted.kill_and_wait(5) == -signal.SIGKILL,
                'adopted hold child final status')
        ownership.inventory(set(), 'unexpected post-reap descendant')
        passed = True
    except BaseException as error:
        ownership.record_uncertainty(coordinator, adopted)
        errors.append('crash experiment: '+repr(error))
    finally:
        os.close(cw); os.close(rr)
        # Only exact verified children may be stopped; unknown spawn/adoption is
        # retained uncertainty. A signal delivery or watchdog exit is not proof.
        try:
            if coordinator.pid is not None and coordinator.status is None:
                coordinator.kill_and_wait(5)
            if reported_pid is not None and adopted.pid is None:
                adopted.verify_adoption(reported_pid)
            if adopted.pid is not None and adopted.status is None:
                adopted.kill_and_wait(5)
        except BaseException as error:
            ownership.record_uncertainty(coordinator, adopted)
            errors.append('crash child cleanup: '+repr(error))
        absent = (coordinator.status is not None and not coordinator.uncertain and
                  adopted.status is not None and not adopted.uncertain)
        ownership.record_uncertainty(coordinator, adopted)
        try:
            ownership.inventory(set(), 'unexpected final child inventory')
            empty = True
        except BaseException as error:
            empty = False
            errors.append('crash inventory: '+repr(error))
        if journal is not None and absent and empty and ownership.eligible:
            try:
                real_recovery(journal)
                real_recovery(journal)
                require(not os.path.lexists(expected_scope), 'crash scope retained')
                print('REMOVED_SCOPE='+str(expected_scope), flush=True)
            except BaseException as error:
                errors.append('crash recovery: '+repr(error))
        else:
            errors.append('crash lifetime/ownership UNCONFIRMED; independent recovery required')
        if errors:
            print('READ_POLICY_CRASH_FAIL='+json.dumps(errors), flush=True)
            print('RETAINED_OR_RECEIPT_JOURNAL='+str(expected_journal), flush=True)
        require(passed and not errors, 'crash experiment/cleanup did not pass')
    print('READ_POLICY_COORDINATOR_LOSS_RECOVERY_PASS', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', action='store_true',
                        help='explicit reviewed root modeled matrix and crash fixture')
    args = parser.parse_args()
    require(args.run, 'explicit --run required; no default policy operation')
    run_matrix()
    run_crash()
    print('READ_POLICY_FIXTURE_PASS_ROOT_WRITER_MODELED_HANDOFF_ONLY', flush=True)


if __name__ == '__main__':
    main()
