# SPDX-License-Identifier: Apache-2.0
"""Private fixed-reference context launcher, not a policy/recovery orchestrator.

The caller validates the fixed image/durable journal and stores an exclusive
OwnedChild BEFORE calling spawn. It owns all supplied FDs exclusively and keeps
them stable through return. The coordinator is structurally single-threaded with
default SIGCHLD, no competing waiter or native/signal path mutating its FD table.
No contents, SQLite, plan/directory FD, policy operation or lock conversion occurs.
Code/image provenance and actual flock SH are external setup premises; metadata
does not establish them. Child FD4 retains the shared description to kernel exit.
"""
import errno
import fcntl
import os
import signal
import stat


def require(value, why):
    if not value:
        raise RuntimeError(why)


def spawn(child, image, role, recovery_fd, stdio):
    require(role in ('system301-platform', 'root-user-shell'), 'fixed role')
    require(child.pid is None and child.status is None and not child.uncertain,
            'unused preallocated exclusive child required')
    require(len(os.listdir('/proc/self/task')) == 1,
            'single-thread reference coordinator')
    require(signal.getsignal(signal.SIGCHLD) == signal.SIG_DFL,
            'default SIGCHLD/exclusive waiter required')
    require(len(stdio) == 3, 'explicit stdio required')
    held = os.fstat(recovery_fd)
    for fd, access in zip(stdio, (os.O_RDONLY, os.O_WRONLY, os.O_WRONLY)):
        require(fd >= 0, 'stdio missing')
        flags = fcntl.fcntl(fd, fcntl.F_GETFL)
        info = os.fstat(fd)
        require((info.st_dev, info.st_ino) != (held.st_dev, held.st_ino),
                'stdio aliases journal')
        require(not stat.S_ISSOCK(info.st_mode) and not flags & os.O_PATH and
                flags & os.O_ACCMODE in (access, os.O_RDWR), 'stdio access')
    flags = fcntl.fcntl(recovery_fd, fcntl.F_GETFL)
    require(stat.S_ISREG(held.st_mode) and held.st_nlink == 1 and
            stat.S_IMODE(held.st_mode) == 0o600 and not flags & os.O_PATH and
            flags & os.O_ACCMODE == os.O_RDWR, 'journal reference setup')
    # Root ownership/ACL and exact identity are revalidated by the real image.
    # This mapping helper also runs unprivileged fake-image mechanics tests.
    argv = [str(image), '--reference-context', role,
            str(held.st_dev), str(held.st_ino)]
    sources = []
    prior_mask = None
    try:
        # All stable sources are allocated before the final snapshot; none can
        # be clobbered by target0..4 mappings, even when parent stdio is closed.
        for fd in (*stdio, recovery_fd):
            sources.append(fcntl.fcntl(fd, fcntl.F_DUPFD_CLOEXEC, 5))
        actions = [(os.POSIX_SPAWN_DUP2, fd, target)
                   for fd, target in zip(sources, (0, 1, 2, 4))]
        actions.append((os.POSIX_SPAWN_CLOSE, 3))  # closefrom(5) cannot do this
        blocked = signal.valid_signals()-{signal.SIGKILL, signal.SIGSTOP}
        prior_mask = signal.pthread_sigmask(signal.SIG_BLOCK, blocked)
        # Own FD metadata only. The transient listdir FD has closed by return;
        # its stale number may be EBADF, all other scan failures reject.
        for name in os.listdir('/proc/self/fd'):
            fd = int(name)
            if fd < 5:
                continue
            try:
                fcntl.fcntl(fd, fcntl.F_GETFD)
            except OSError as error:
                require(error.errno == errno.EBADF, 'complete own FD snapshot')
                continue
            actions.append((os.POSIX_SPAWN_CLOSE, fd))
        # Signal raising is deferred through positive PID attachment. A Python
        # failure across libc return/assignment is uncertain, never no-child.
        child.uncertain = True
        pid = os.posix_spawn(str(image), argv,
                             {'PATH': '/usr/bin:/bin', 'LANG': 'C',
                              'PYTHONDONTWRITEBYTECODE': '1',
                              'PYTHONNOUSERSITE': '1'},
                             file_actions=actions, setsigmask=(),
                             setsigdef=(signal.SIGCHLD, signal.SIGPIPE,
                                        signal.SIGHUP, signal.SIGTERM,
                                        signal.SIGINT, signal.SIGALRM))
        child.attach_direct(pid)  # no handshake/logging before ownership
    finally:
        try:
            close_error = None
            for fd in sources:
                try:
                    os.close(fd)  # parent duplicates only; no F_UNLCK/LOCK_UN
                except OSError as error:
                    close_error = error
            if close_error is not None:
                raise close_error
        finally:
            if prior_mask is not None:
                signal.pthread_sigmask(signal.SIG_SETMASK, prior_mask)
