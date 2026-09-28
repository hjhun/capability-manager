# SPDX-License-Identifier: Apache-2.0
"""Private causal fixture child records; no policy operation on import.

Only a structurally single-threaded subreaper with default SIGCHLD and no competing
waiter may use these records. A reported PID alone never grants signaling authority.
No task credentials, identity or liveness is inferred from procfs or flock.
"""
import ctypes
import os
import signal
import time


def require(okay, cause):
    if not okay:
        raise RuntimeError(cause)


def enable_subreaper():
    require(signal.getsignal(signal.SIGCHLD) == signal.SIG_DFL,
            'default SIGCHLD/exclusive waiter required')
    libc = ctypes.CDLL(None, use_errno=True)
    require(libc.prctl(36, 1, 0, 0, 0) == 0, 'set fixture child subreaper')
    observed = ctypes.c_int()
    require(libc.prctl(37, ctypes.byref(observed), 0, 0, 0) == 0 and
            observed.value == 1, 'verify fixture child subreaper')


class OwnedChild:
    """Unreaped exact direct/adopted child; no signal after terminal observation."""
    def __init__(self):
        self.pid = None
        self.status = None
        self.uncertain = False
        self.can_signal = False
        self.exited = False

    def attach_direct(self, pid):
        # Trusted fork/posix_spawn return only, never an arbitrary reported PID.
        require(self.pid is None and self.status is None and not self.exited and pid > 0,
                'invalid direct child attach')
        self.pid = pid
        self.can_signal = True
        self.uncertain = False  # last: keep pre-spawn uncertainty until attached

    def verify_adoption(self, reported_pid):
        require(self.pid is None and not self.uncertain and reported_pid > 0,
                'invalid adoption record')
        self.pid = reported_pid
        # Set uncertainty before the kernel query; ECHILD cannot authorize kill.
        self.uncertain = True
        observed = os.waitid(os.P_PID, self.pid,
                             os.WEXITED | os.WNOHANG | os.WNOWAIT)
        require(observed is None or observed.si_pid == self.pid,
                'wrong adopted child observation')
        self.exited = observed is not None
        self.can_signal = not self.exited
        self.uncertain = False
        return not self.exited

    def observe(self):
        require(self.pid is not None and not self.uncertain,
                'child ownership uncertain')
        if self.status is not None:
            return True
        try:
            result = os.waitid(os.P_PID, self.pid,
                               os.WEXITED | os.WNOHANG | os.WNOWAIT)
            require(result is None or result.si_pid == self.pid,
                    'wrong owned child observation')
        except BaseException:
            self.can_signal = False
            self.uncertain = True
            raise
        if result is not None:
            self.exited = True
            self.can_signal = False
        return self.exited

    def reap(self):
        require(self.exited and not self.uncertain and self.status is None,
                'no terminal owned child proof')
        self.can_signal = False  # recorded BEFORE final reap can free the PID
        self.uncertain = True
        pid, status = os.waitpid(self.pid, os.WNOHANG)
        require(pid == self.pid, 'owned reap incomplete')
        self.status = os.waitstatus_to_exitcode(status)
        self.uncertain = False
        return self.status

    def wait(self, seconds):
        end = time.monotonic()+seconds
        while self.status is None and time.monotonic() < end:
            if self.observe():
                return self.reap()
            time.sleep(min(.01, max(0, end-time.monotonic())))
        return self.status

    def kill_and_wait(self, seconds):
        if self.status is not None:
            return self.status
        if not self.observe():
            require(self.can_signal and not self.uncertain,
                    'no owned live-child signal authority')
            try:
                os.kill(self.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass  # ESRCH is not absence; only exact observe/reap can finish
        result = self.wait(seconds)
        require(result is not None, 'child cleanup UNCONFIRMED')
        return result
