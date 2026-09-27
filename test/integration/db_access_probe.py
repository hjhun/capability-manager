#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Explicit root-only SQLite WAL DAC/SMACK fixture; never a production policy installer.

Creates only a UUID directory/labels/rules, forks fixed reader/writer roles, and
revokes its rules after confirmed child exit. Uses neither immutable nor DB write
permission for readers. Security-manager privilege-to-label provisioning remains
an independent integration gate. Run under a >=180s watchdog; normal role waits
and cleanup have a smaller total budget. A root-owned journal and inherited flock
make SIGKILL recovery explicit: rerun with --recover PRINTED_JOURNAL after the
watchdog/roles exit. Recovery refuses while any fixed role retains the lock.
"""
import argparse
import ctypes
import errno
import fcntl
import grp
import json
import os
from pathlib import Path
import pwd
import re
import select
import shutil
import signal
import sqlite3
import stat
import sys
import time
import uuid


def require(value, message):
    if not value:
        raise RuntimeError(message)


def label():
    return Path('/proc/self/attr/current').read_bytes().rstrip(b'\0\n').decode()


def rule(subject, object_label, access):
    require(all(x and not any(c.isspace() for c in x) and '\0' not in x
                for x in (subject, object_label)), 'invalid fixture label')
    with open('/sys/fs/smackfs/load2', 'w') as stream:
        stream.write(f'{subject} {object_label} {access}\n')


def drop(uid, gid, groups, role_label):
    Path('/proc/self/attr/current').write_text(role_label)
    os.setgroups(groups)
    os.setresgid(gid, gid, gid)
    os.setresuid(uid, uid, uid)
    libc = ctypes.CDLL(None, use_errno=True)
    libc.prctl.argtypes = [ctypes.c_int, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong]
    require(libc.prctl(38, 1, 0, 0, 0) == 0, 'no_new_privs')
    status = Path('/proc/self/status').read_text().splitlines()
    caps = {line.split(':')[0]: line.split(':')[1].strip() for line in status if line.startswith('Cap')}
    require(int(caps['CapEff'], 16) == 0 and int(caps['CapPrm'], 16) == 0, 'retained privilege')
    require(os.getresuid() == (uid, uid, uid) and os.getresgid() == (gid, gid, gid), 'role IDs')
    require(label() == role_label, 'role label')


def line(fd, seconds=5):
    deadline = time.monotonic() + seconds
    data = bytearray()
    while time.monotonic() < deadline:
        readable, _, _ = select.select([fd], [], [], max(0, deadline-time.monotonic()))
        require(readable, 'fixture pipe deadline')
        byte = os.read(fd, 1)
        require(byte, 'fixture pipe EOF')
        if byte == b'\n':
            return data.decode()
        data.extend(byte)
        require(len(data) < 4096, 'fixture message limit')
    raise RuntimeError('fixture pipe deadline')


def send(fd, message):
    data = (message + '\n').encode()
    require(len(data) < 4096 and os.write(fd, data) == len(data), 'fixture send')


def reap(pid, seconds=5):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        observed, status = os.waitpid(pid, os.WNOHANG)
        if observed:
            return os.waitstatus_to_exitcode(status)
        time.sleep(.01)
    return None


def reader(db, expected, readable, role_label, uid, gid, groups):
    drop(uid, gid, groups, role_label)
    if not readable:
        try:
            fd = os.open(db, os.O_RDONLY)
        except PermissionError as error:
            require(error.errno in (errno.EACCES, errno.EPERM), 'unexpected denial')
            return
        os.close(fd)
        raise RuntimeError('unauthorized reader opened database')
    connection = sqlite3.connect('file:' + str(db) + '?mode=ro', uri=True, timeout=1)
    require(connection.execute('SELECT value FROM fixture').fetchone()[0] == expected, 'wrong committed value')
    try:
        connection.execute('INSERT INTO fixture VALUES(999)')
    except sqlite3.OperationalError:
        pass
    else:
        raise RuntimeError('readonly connection wrote')
    connection.close()
    for path in (db, Path(str(db)+'-wal'), Path(str(db)+'-shm')):
        try:
            fd = os.open(path, os.O_RDWR)
        except PermissionError:
            continue
        else:
            os.close(fd)
            raise RuntimeError('reader has file write access: ' + path.name)
    # DAC permits this file and directory: denial here isolates the MAC write
    # restriction, unlike the production-shaped 0640/2750 assertions above.
    for path, flags in ((db.parent/'mac-write-probe', os.O_WRONLY),
                        (db.parent/'mac-directory'/'forbidden', os.O_WRONLY | os.O_CREAT | os.O_EXCL)):
        try:
            fd = os.open(path, flags, 0o600)
        except PermissionError:
            continue
        os.close(fd)
        raise RuntimeError('SMACK allowed write despite readonly rule')
    try:
        fd = os.open(db.parent/'forbidden', os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    except PermissionError:
        pass
    else:
        os.close(fd)
        raise RuntimeError('reader has directory write access')


RECOVERY_PARENT = Path('/opt/usr')


def trusted_parent():
    # This explicit root-operated image fixture trusts the platform's procfs and
    # PID1. It refuses other mount/PID namespaces and non-local backing stores.
    # A hostile root/platform mount administrator is outside the fixture threat
    # model; unprivileged app_fw peers must not rename any ancestor entry.
    for kind in ('mnt', 'pid'):
        left, right = os.stat('/proc/self/ns/'+kind), os.stat('/proc/1/ns/'+kind)
        require((left.st_dev, left.st_ino) == (right.st_dev, right.st_ino),
                'fixture must use platform PID1 namespaces')
    for path in (Path('/'), Path('/opt'), RECOVERY_PARENT):
        info = path.lstat()
        require(stat.S_ISDIR(info.st_mode) and info.st_uid == 0 and
                not (info.st_mode & 0o022), 'unsafe recovery ancestor: '+str(path))
        for attribute in ('system.posix_acl_access', 'system.posix_acl_default'):
            try:
                os.getxattr(path, attribute, follow_symlinks=False)
            except OSError as error:
                require(error.errno in (errno.ENODATA, errno.EOPNOTSUPP), 'ACL check failed')
            else:
                raise RuntimeError('recovery ancestor ACL is unsupported: '+str(path))
    mounts = []
    for line in Path('/proc/self/mountinfo').read_text().splitlines():
        before, after = line.split(' - ', 1)
        fields, backing = before.split(), after.split()
        point = fields[4]
        if point == '/' or str(RECOVERY_PARENT) == point or str(RECOVERY_PARENT).startswith(point+'/'):
            mounts.append((len(point), backing[0], backing[1]))
    require(mounts, 'recovery backing mount absent')
    _, filesystem, source = max(mounts)
    require(filesystem == 'ext4' and source.startswith('/dev/'),
            'fixture requires platform-owned local ext4 recovery mount')


def persist(path, value):
    # Root-owned journal directory; atomic replacement plus fsync before policy writes.
    temporary = path.with_suffix('.new')
    fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_TRUNC | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, 'w') as stream:
        json.dump(value, stream)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)
    fd = os.open(path.parent, os.O_DIRECTORY | os.O_CLOEXEC)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def cleanup_scope(plan):
    """Call only while holding the exclusive journal lock (all roles are gone)."""
    errors, remaining = [], []
    try:
        root = Path(plan['scope'])
        if root.exists() or root.is_symlink():
            info = root.lstat()
            require(stat.S_ISDIR(info.st_mode) and
                    [info.st_dev, info.st_ino] == plan['scope_identity'], 'scope identity changed')
            require(shutil.rmtree.avoids_symlink_attacks, 'safe rmtree unavailable')
            shutil.rmtree(root)
    except BaseException as error:
        errors.append('directory: '+str(error))
    # Directory failure must not skip policy revocation. All pairs are unique to
    # this UUID; retrying revocation after a crash is idempotent.
    for subject, target, _ in plan['rules']:
        try:
            rule(subject, target, '------')
        except BaseException as error:
            remaining.append([subject, target])
            errors.append('revoke '+subject+' -> '+target+': '+str(error))
    return {'cleanup': 'PASS' if not errors else 'BLOCKED',
            'cleanup_errors': errors, 'remaining_rules': remaining}


def recover(journal):
    trusted_parent()
    # The journal is root-owned and outside the app_fw-owned fixture tree.
    require(journal.is_absolute() and journal.parent == RECOVERY_PARENT and
            re.fullmatch(r'capmgr-db-recovery-[0-9a-f]{32}', journal.name), 'invalid journal path')
    info = journal.lstat()
    require(stat.S_ISDIR(info.st_mode) and info.st_uid == 0 and
            stat.S_IMODE(info.st_mode) == 0o700, 'untrusted journal')
    fd = os.open(journal/'lock', os.O_RDWR | os.O_NOFOLLOW | os.O_CLOEXEC)
    try:
        info = os.fstat(fd)
        require(stat.S_ISREG(info.st_mode) and info.st_uid == 0 and
                stat.S_IMODE(info.st_mode) == 0o600, 'untrusted lock')
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise RuntimeError('recovery BLOCKED: fixture parent/child still holds lock')
        plan_fd = os.open(journal/'plan.json', os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC)
        with os.fdopen(plan_fd) as stream:
            info = os.fstat(stream.fileno())
            require(stat.S_ISREG(info.st_mode) and info.st_uid == 0 and
                    stat.S_IMODE(info.st_mode) == 0o600 and info.st_size <= 32768, 'untrusted plan')
            plan = json.load(stream)
        require(plan['journal'] == str(journal) and
                plan['scope'] == str(RECOVERY_PARENT/('capmgr-db-access-'+journal.name[-32:])),
                'journal scope mismatch')
        result = cleanup_scope(plan)
        persist(journal/'recovery.json', result)
        print(json.dumps(result), flush=True)
        return 0 if result['cleanup'] == 'PASS' else 1
    finally:
        os.close(fd)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', action='store_true', help='Explicitly run the privileged fixture')
    parser.add_argument('--recover', type=Path, help='Recover a printed root-owned journal after abrupt termination')
    args = parser.parse_args()
    if args.run == bool(args.recover):
        parser.error('choose exactly one of --run or --recover')
    require(os.geteuid() == 0, 'root fixture required')
    require(Path('/sys/fs/smackfs/load2').is_file(), 'SMACK load2 unavailable')
    require(shutil.rmtree.avoids_symlink_attacks, 'safe rmtree unavailable')
    if args.recover:
        return recover(args.recover)
    trusted_parent()
    signal.signal(signal.SIGCHLD, signal.SIG_DFL)
    account = pwd.getpwnam('app_fw')
    group = grp.getgrnam('priv_platform')
    require(account.pw_uid != 0, 'invalid app_fw identity')
    key = uuid.uuid4().hex
    journal = RECOVERY_PARENT/('capmgr-db-recovery-'+key)
    journal.mkdir(mode=0o700)
    lock_fd = os.open(journal/'lock', os.O_CREAT | os.O_EXCL | os.O_RDWR | os.O_CLOEXEC, 0o600)
    # This inherited open-file description stays held by every fixed fork role.
    # Recovery uses a separate open description; it cannot acquire EX until ALL
    # parent/child references have closed, including after watchdog SIGKILL.
    fcntl.flock(lock_fd, fcntl.LOCK_SH)
    root = RECOVERY_PARENT/('capmgr-db-access-'+key)
    root.mkdir(mode=0o700)
    database_label = 'CapMgrFixture::DB::'+key
    allowed_label = 'CapMgrFixture::Read::'+key
    denied_label = 'CapMgrFixture::Deny::'+key
    rules = []
    plan = None
    children = set()
    descriptors = []
    result = {'scope': str(root), 'status': 'FAIL', 'production_policy': 'NOT_RUN', 'checks': []}
    try:
        for subject, access in ((label(), 'rwxatl'), ('System', 'rwxatl'), (allowed_label, 'rxl')):
            if not any(pair[:2] == (subject, database_label) for pair in rules):
                rules.append((subject, database_label, access))
        # Only new UUID subjects gain ancestor traversal; no existing subject's
        # access to an existing label is changed. Floor labels need no new rule.
        for ancestor in (Path('/'), Path('/opt'), Path('/opt/usr')):
            try:
                ancestor_label = os.getxattr(ancestor, 'security.SMACK64').rstrip(b'\0').decode()
            except OSError as error:
                if error.errno != errno.ENODATA:
                    raise
                ancestor_label = '_'
            if ancestor_label == '_':
                continue
            for subject in (allowed_label, denied_label):
                if not any(pair[:2] == (subject, ancestor_label) for pair in rules):
                    rules.append((subject, ancestor_label, 'x'))
        info = root.stat()
        plan = {'scope': str(root), 'scope_identity': [info.st_dev, info.st_ino],
                'journal': str(journal), 'rules': rules}
        persist(journal/'plan.json', plan)
        print(json.dumps({'recovery_manifest': str(journal/'plan.json'),
                          'recover_command': [sys.executable, str(Path(__file__).resolve()), '--recover', str(journal)],
                          'scope': str(root)}), flush=True)
        for subject, target, access in rules:
            rule(subject, target, access)
        os.setxattr(root, 'security.SMACK64', database_label.encode())
        os.setxattr(root, 'security.SMACK64TRANSMUTE', b'TRUE')
        os.chown(root, account.pw_uid, group.gr_gid)
        os.chmod(root, 0o2750)
        probe = root/'mac-write-probe'
        probe.touch(mode=0o666)
        os.chmod(probe, 0o666)
        os.setxattr(probe, 'security.SMACK64', database_label.encode())
        probe_directory = root/'mac-directory'
        probe_directory.mkdir(mode=0o777)
        os.chmod(probe_directory, 0o777)
        os.setxattr(probe_directory, 'security.SMACK64', database_label.encode())
        db = root/'catalog.db'
        command_r, command_w = os.pipe2(os.O_CLOEXEC)
        status_r, status_w = os.pipe2(os.O_CLOEXEC)
        descriptors += [command_r, command_w, status_r, status_w]
        writer = os.fork()
        if writer == 0:
            os.close(command_w)
            os.close(status_r)
            try:
                drop(account.pw_uid, account.pw_gid, [], 'System')
                os.umask(0o027)
                for generation in (1, 2):
                    connection = sqlite3.connect(db, timeout=1)
                    require(connection.execute('PRAGMA journal_mode=WAL').fetchone()[0] == 'wal', 'WAL unavailable')
                    connection.execute('PRAGMA wal_autocheckpoint=0')
                    connection.execute('CREATE TABLE IF NOT EXISTS fixture(value INTEGER)')
                    connection.execute('DELETE FROM fixture')
                    connection.execute('INSERT INTO fixture VALUES(?)', (generation,))
                    connection.commit()
                    send(status_w, 'READY '+str(generation))
                    require(line(command_r, 30) == 'CLOSE', 'writer command')
                    connection.close()
                    send(status_w, 'CLOSED '+str(generation))
                    if generation == 1:
                        require(line(command_r, 30) == 'REOPEN', 'writer reopen')
                os._exit(0)
            except BaseException as error:
                try:
                    send(status_w, 'ERROR '+str(error))
                finally:
                    os._exit(1)
        children.add(writer)
        os.close(command_r); descriptors.remove(command_r)
        os.close(status_w); descriptors.remove(status_w)
        for generation in (1, 2):
            require(line(status_r) == 'READY '+str(generation), 'writer readiness')
            for path in (db, Path(str(db)+'-wal'), Path(str(db)+'-shm')):
                info = path.stat()
                require(info.st_uid == account.pw_uid and info.st_gid == group.gr_gid, 'sidecar owner/group')
                require(stat.S_IMODE(info.st_mode) == 0o640, 'sidecar mode')
                require(os.getxattr(path, 'security.SMACK64').rstrip(b'\0') == database_label.encode(), 'sidecar label')
            cases = [('allowed', True, allowed_label, 65532, 65532, [group.gr_gid]),
                     ('denied-smack', False, denied_label, 65532, 65532, [group.gr_gid]),
                     ('denied-dac', False, allowed_label, 65532, 65532, []),
                     ('same-uid-cli-denied', False, denied_label, account.pw_uid, account.pw_gid, [])]
            for name, permitted, role_label, uid, gid, groups in cases:
                child = os.fork()
                if child == 0:
                    try:
                        os.close(command_w); os.close(status_r)
                        reader(db, generation, permitted, role_label, uid, gid, groups)
                        os._exit(0)
                    except BaseException as error:
                        print(name+': '+str(error), file=sys.stderr, flush=True)
                        os._exit(1)
                children.add(child)
                code = reap(child)
                if code is not None:
                    children.remove(child)
                require(code == 0, name+' failed: '+str(code))
                result['checks'].append({'generation': generation, 'case': name, 'exit': code})
            send(command_w, 'CLOSE')
            require(line(status_r) == 'CLOSED '+str(generation), 'writer close')
            require(not Path(str(db)+'-wal').exists() and not Path(str(db)+'-shm').exists(), 'sidecars not removed after last close')
            if generation == 1:
                send(command_w, 'REOPEN')
        code = reap(writer)
        if code is not None:
            children.remove(writer)
        require(code == 0, 'writer exit')
        result['status'] = 'PASS'
    except BaseException as error:
        result['error'] = str(error)
    finally:
        cleanup_errors = []
        for child in list(children):
            # Exclusive unreaped direct children: no numeric PID can be reused.
            try:
                try:
                    os.kill(child, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                code = reap(child)
                if code is not None:
                    children.remove(child)
            except BaseException as error:
                cleanup_errors.append('child '+str(child)+': '+str(error))
        for fd in descriptors:
            try:
                os.close(fd)
            except OSError as error:
                cleanup_errors.append('descriptor: '+str(error))
        if not children:
            # LOCK_UN on the shared open description would release other roles'
            # lock too; only upgrade after every direct fixed-role child is reaped.
            fcntl.flock(lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            if plan is not None:
                result.update(cleanup_scope(plan))
            else:
                try:
                    shutil.rmtree(root)
                    result['cleanup'] = 'PASS'
                except OSError as error:
                    cleanup_errors.append('pre-policy directory: '+str(error))
        result['cleanup_errors'] = result.get('cleanup_errors', []) + cleanup_errors
        result.setdefault('cleanup', 'BLOCKED')
        result['remaining_children'] = sorted(children)
        if result['cleanup_errors'] or result['cleanup'] != 'PASS':
            result['status'] = 'FAIL'
        persist(journal/'result.json', result)
        os.close(lock_fd)
        # Kernel label names may remain until reboot, with fixture rules revoked.
        print(json.dumps(result, indent=2), flush=True)
    return 0 if result['status'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
