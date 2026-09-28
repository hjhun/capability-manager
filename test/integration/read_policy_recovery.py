#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Private causal read-policy fixture journal; not a policy provisioner.

No policy operation runs on import. The eventual root fixture must provide exact
source/binary/preflight review before install_rules or recover is invoked. Every
fixed role retains the same SH open description throughout its process lifetime;
independent recovery refuses until all those references disappear. No PID kill or
watchdog-exit inference is used here.
"""
import errno
import fcntl
import json
import os
from pathlib import Path
import re
import shutil
import stat


PARENT = Path('/opt/usr')
PREFIX = 'capmgr-read-policy-'
JOURNAL_PREFIX = 'capmgr-read-policy-recovery-'


def require(okay, message):
    if not okay:
        raise RuntimeError(message)


def identity(info):
    return [info.st_dev, info.st_ino]


def label_names(key):
    require(re.fullmatch(r'[0-9a-f]{32}', key), 'invalid fixture key')
    return {name: 'CapMgrReadPolicy::'+key+'::'+name
            for name in ('Traversal', 'Catalog', 'Lock', 'Allowed', 'Denied')}


def rule_matrix(key, writer):
    labels = label_names(key)
    require(isinstance(writer, str) and 0 < len(writer.encode()) <= 255 and
            not any(c.isspace() or c == '\0' for c in writer) and
            writer not in labels.values(), 'invalid observed writer label')
    rows = [(writer, ('rwx', 'rwxlt', 'rwl')),
            (labels['Allowed'], ('rx', 'rxl', 'rl')),
            (labels['Denied'], ('rx', 'x', 'rl'))]
    objects = [labels['Traversal'], labels['Catalog'], labels['Lock']]
    return [[subject, target, access] for subject, permissions in rows
            for target, access in zip(objects, permissions)]


class Operations:
    """Default root/image operations; alternate owner is only a host-test seam."""
    owner = 0
    group = 0
    parent = PARENT

    def trusted_parent(self):
        require(os.geteuid() == 0, 'root fixture required')
        # Reuse the accepted image-only proc/PID1/ext4/ancestor check. It is not
        # peer credential authority. Import is deferred; no operational run.
        dependency = Path(__file__).absolute().with_name('db_access_probe.py')
        module = source_module(dependency)
        module.trusted_parent()

    def write_rule(self, subject, target, access):
        with open('/sys/fs/smackfs/load2', 'w') as stream:
            count = stream.write(subject+' '+target+' '+access+'\n')
            require(count == len(subject+' '+target+' '+access+'\n'),
                    'incomplete rule write')

    def delete_scope(self, scope):
        require(shutil.rmtree.avoids_symlink_attacks, 'safe deletion unavailable')
        shutil.rmtree(scope)


def exact(info, owner, mode, directory=False):
    require((stat.S_ISDIR(info.st_mode) if directory else
             stat.S_ISREG(info.st_mode)) and info.st_uid == owner and
            (info.st_mode & 0o7777) == mode and
            (directory or info.st_nlink == 1), 'unsafe journal object')


def no_acl(path):
    for name in ('system.posix_acl_access', 'system.posix_acl_default'):
        try:
            os.getxattr(path, name) if isinstance(path, int) else \
                os.getxattr(path, name, follow_symlinks=False)
        except OSError as error:
            require(error.errno in (errno.ENODATA, errno.EOPNOTSUPP),
                    'journal ACL lookup failed')
        else:
            raise RuntimeError('journal ACL present')


def persist(directory_fd, name, value, owner):
    """Same pinned root-only journal directory, exact single-link atomic state."""
    require(name in ('plan.json', 'recovery.json'), 'invalid state filename')
    data = (json.dumps(value, sort_keys=True, separators=(',', ':'))+'\n').encode()
    require(len(data) <= 32768, 'journal state too large')
    temporary = name+'.next'
    # A stale recovery receipt is not authority. Recovery always replays the
    # immutable complete plan, so it may discard only this validated receipt
    # temporary before retry. An incomplete plan remains a hard failure.
    if name == 'recovery.json':
        try:
            stale = os.open(temporary, os.O_RDONLY | os.O_NOFOLLOW |
                            os.O_CLOEXEC, dir_fd=directory_fd)
        except FileNotFoundError:
            pass
        else:
            try:
                exact(os.fstat(stale), owner, 0o600)
                no_acl(stale)
                os.unlink(temporary, dir_fd=directory_fd)
                os.fsync(directory_fd)
            finally:
                os.close(stale)
    fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL |
                 os.O_NOFOLLOW | os.O_CLOEXEC, 0o600, dir_fd=directory_fd)
    try:
        exact(os.fstat(fd), owner, 0o600)
        no_acl(fd)
        offset = 0
        while offset < len(data):
            written = os.write(fd, data[offset:])
            require(written > 0, 'short journal write')
            offset += written
        os.fsync(fd)
    finally:
        os.close(fd)
    os.rename(temporary, name, src_dir_fd=directory_fd,
              dst_dir_fd=directory_fd)
    os.fsync(directory_fd)


def load_plan(directory_fd, owner):
    fd = os.open('plan.json', os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC,
                 dir_fd=directory_fd)
    try:
        info = os.fstat(fd)
        exact(info, owner, 0o600)
        no_acl(fd)
        require(0 < info.st_size <= 32768, 'invalid plan size')
        data = bytearray()
        while len(data) <= 32768:
            chunk = os.read(fd, 4096)
            if not chunk:
                break
            data.extend(chunk)
        require(len(data) == info.st_size, 'plan size changed')
        def unique(pairs):
            result = {}
            for key, value in pairs:
                require(key not in result, 'duplicate plan key')
                result[key] = value
            return result
        return json.loads(data, object_pairs_hook=unique)
    finally:
        os.close(fd)


def validate_plan(plan, journal, ops, directory, lock):
    fields = {'version', 'key', 'writer', 'journal', 'journal_identity',
              'lock_identity', 'scope', 'scope_identity', 'parent_identity',
              'rules'}
    require(isinstance(plan, dict) and set(plan) == fields and
            type(plan['version']) is int and plan['version'] == 1,
            'unsupported journal plan')
    key = plan['key']
    require(plan['journal'] == str(journal) and
            journal == ops.parent/(JOURNAL_PREFIX+key) and
            plan['scope'] == str(ops.parent/(PREFIX+key)), 'plan path mismatch')
    for field in ('journal_identity', 'lock_identity', 'scope_identity',
                  'parent_identity'):
        pair = plan[field]
        require(isinstance(pair, list) and len(pair) == 2 and
                all(type(x) is int and x >= 0 for x in pair), 'invalid identity')
    require(identity(directory) == plan['journal_identity'] and
            identity(lock) == plan['lock_identity'] and
            identity(ops.parent.lstat()) == plan['parent_identity'],
            'journal/parent/lock identity changed')
    require(plan['rules'] == rule_matrix(key, plan['writer']),
            'unexpected rule matrix')
    return Path(plan['scope'])


def recover(journal, ops=None):
    """Only explicit independent EX; all deletion/revocation failures retained."""
    ops = ops or Operations()
    ops.trusted_parent()
    journal = Path(journal)
    require(journal.is_absolute() and journal.parent == ops.parent and
            re.fullmatch(JOURNAL_PREFIX+r'[0-9a-f]{32}', journal.name),
            'invalid journal path')
    initial = journal.lstat()
    exact(initial, ops.owner, 0o700, directory=True)
    no_acl(journal)
    directory_fd = os.open(journal, os.O_RDONLY | os.O_DIRECTORY |
                           os.O_NOFOLLOW | os.O_CLOEXEC)
    lock_fd = -1
    try:
        held = os.fstat(directory_fd)
        exact(held, ops.owner, 0o700, directory=True)
        require(identity(initial) == identity(held), 'journal replaced')
        lock_fd = os.open('lock', os.O_RDWR | os.O_NOFOLLOW | os.O_CLOEXEC,
                          dir_fd=directory_fd)
        lock = os.fstat(lock_fd)
        exact(lock, ops.owner, 0o600)
        no_acl(lock_fd)
        require(lock.st_gid == ops.group, 'journal lock group changed')
        try:
            fcntl.flock(lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise RuntimeError('recovery BLOCKED: live SH reference') from error
        require(held.st_gid == ops.group, 'journal group changed')
        plan = load_plan(directory_fd, ops.owner)
        scope = validate_plan(plan, journal, ops, held, lock)
        errors, remaining = [], []
        try:
            if scope.exists() or scope.is_symlink():
                current = scope.lstat()
                exact(current, ops.owner, 0o755, directory=True)
                require(current.st_gid == ops.group, 'scope group changed')
                no_acl(scope)
                require(identity(current) == plan['scope_identity'],
                        'scope identity changed')
                require(identity(journal.lstat()) == identity(held) and
                        identity(ops.parent.lstat()) == plan['parent_identity'],
                        'scope ancestry changed')
                ops.delete_scope(scope)
                require(not scope.exists() and not scope.is_symlink(),
                        'scope still present')
        except BaseException as error:
            errors.append('scope: '+str(error))
        for subject, target, _ in plan['rules']:
            try:
                ops.write_rule(subject, target, '------')
            except BaseException as error:
                remaining.append([subject, target])
                errors.append('revoke '+subject+' -> '+target+': '+str(error))
        result = {'cleanup': 'PASS' if not errors else 'BLOCKED',
                  'errors': errors, 'remaining_rules': remaining}
        # If this persistence fails, caller must report FAIL and retain original
        # durable plan; a successful write return is not a kernel rule audit.
        persist(directory_fd, 'recovery.json', result, ops.owner)
        return result
    finally:
        if lock_fd >= 0:
            os.close(lock_fd)
        os.close(directory_fd)


class JournalOwner:
    """Durable plan plus never-unlocked SH lifetime; no child/PID inference.

    The caller creates the scope before journal creation, before any policy
    write. Fixed roles inherit only lock_fd and retain it until process exit.
    close() closes this process's references, never issues LOCK_UN. It does not
    authorize cleanup; only independently acquired recovery EX does that.
    """
    def __init__(self, key, writer, scope, ops=None):
        self.ops = ops or Operations()
        self.ops.trusted_parent()
        self.directory_fd = self.lock_fd = -1
        self.ready = False
        self.rules = rule_matrix(key, writer)
        self.path = self.ops.parent/(JOURNAL_PREFIX+key)
        scope = Path(scope)
        require(scope == self.ops.parent/(PREFIX+key), 'invalid owned scope')
        scope_info = scope.lstat()
        exact(scope_info, self.ops.owner, 0o755, directory=True)
        require(scope_info.st_gid == self.ops.group, 'unexpected scope group')
        no_acl(scope)
        os.mkdir(self.path, 0o700)
        try:
            no_acl(self.path)
            self.directory_fd = os.open(self.path, os.O_RDONLY |
                os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC)
            held = os.fstat(self.directory_fd)
            exact(held, self.ops.owner, 0o700, directory=True)
            require(held.st_gid == self.ops.group, 'unexpected journal group')
            self.lock_fd = os.open('lock', os.O_RDWR | os.O_CREAT | os.O_EXCL |
                os.O_NOFOLLOW | os.O_CLOEXEC, 0o600, dir_fd=self.directory_fd)
            lock = os.fstat(self.lock_fd)
            exact(lock, self.ops.owner, 0o600)
            no_acl(self.lock_fd)
            require(lock.st_gid == self.ops.group, 'unexpected lock group')
            fcntl.flock(self.lock_fd, fcntl.LOCK_SH | fcntl.LOCK_NB)
            plan = {'version': 1, 'key': key, 'writer': writer,
                    'journal': str(self.path), 'journal_identity': identity(held),
                    'lock_identity': identity(lock), 'scope': str(scope),
                    'scope_identity': identity(scope_info),
                    'parent_identity': identity(self.ops.parent.lstat()),
                    'rules': self.rules}
            persist(self.directory_fd, 'plan.json', plan, self.ops.owner)
            parent_fd = os.open(self.ops.parent, os.O_RDONLY | os.O_DIRECTORY |
                                os.O_NOFOLLOW | os.O_CLOEXEC)
            try:
                os.fsync(parent_fd)
            finally:
                os.close(parent_fd)
            self.ready = True
        except BaseException:
            self.close()
            # No rule is permitted before successful construction. Retain the
            # journal for diagnosis; never claim it is recoverable without plan.
            raise

    def install_rules(self, recovery_command):
        require(self.ready and self.lock_fd >= 0, 'journal not durable/live')
        require(isinstance(recovery_command, list) and
                len(recovery_command) == 4 and
                all(isinstance(x, str) for x in recovery_command) and
                Path(recovery_command[0]).is_absolute() and
                Path(recovery_command[1]).is_absolute() and
                recovery_command[2:] == ['--recover', str(self.path)],
                'invalid absolute recovery command')
        # Exact argv is printed and flushed before the first attempted write.
        print('RECOVERY_ARGV='+json.dumps(recovery_command), flush=True)
        for subject, target, access in self.rules:
            self.ops.write_rule(subject, target, access)

    def close(self):
        self.ready = False
        errors = []
        for name in ('lock_fd', 'directory_fd'):
            fd = getattr(self, name)
            if fd >= 0:
                setattr(self, name, -1)
                try:
                    os.close(fd)
                except OSError as error:
                    errors.append(error)
        if errors:
            raise errors[0]


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


def main(argv=None):
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--recover', type=Path, required=True)
    args = parser.parse_args(argv)
    require(os.geteuid() == 0, 'root recovery required')
    # No deployment, creation, role spawn or rule-install CLI exists here.
    trusted_source(Path(__file__).absolute())
    result = recover(args.recover)
    print('READ_POLICY_RECOVERY='+json.dumps(result, sort_keys=True), flush=True)
    return 0 if result['cleanup'] == 'PASS' else 1


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except Exception as error:
        import sys
        print('READ_POLICY_RECOVERY_FAIL='+str(error), file=sys.stderr, flush=True)
        raise SystemExit(1)
