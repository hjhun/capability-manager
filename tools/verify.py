#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Host/SDB preflight, build, fixture integration, smoke and catalog performance."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tarfile
import time
import uuid


def archive_safe(path, expected_root):
    if path.stat().st_size > 128 * 1024 * 1024:
        raise ValueError("compressed archive exceeds 128 MiB")
    with tarfile.open(path) as archive:
        total = 0
        for count, member in enumerate(archive, 1):
            total += member.size
            if count > 100000 or total > 512 * 1024 * 1024:
                raise ValueError("archive extraction budget exceeded")
            parts = Path(member.name).parts
            if not parts or parts[0] != expected_root or member.name.startswith('/') or '..' in parts:
                raise ValueError('unsafe archive member: ' + member.name)
            if not (member.isfile() or member.isdir()):
                raise ValueError('unsupported archive member type: ' + member.name)



class Runner:
    def __init__(self, args):
        self.args = args
        self.output = args.output.resolve()
        if self.output.exists() and any(self.output.iterdir()):
            raise ValueError("Output directory is not empty; preserve the previous evidence")
        self.output.mkdir(parents=True, exist_ok=True)
        self.steps = []
        self.remote_root = None
        self.inputs = []

    def run(self, name, argv, timeout=300):
        start = time.monotonic()
        try:
            result = subprocess.run(argv, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True, errors="replace",
                                    timeout=timeout, check=False)
            code, output = result.returncode, result.stdout
        except subprocess.TimeoutExpired as error:
            code = 124
            output = error.stdout or b''
            if isinstance(output, bytes):
                output = output.decode(errors='replace')
        except OSError as error:
            code, output = 127, str(error)
        (self.output / (name + '.log')).write_text(output)
        self.steps.append({'name': name, 'argv': argv, 'exit': code,
                           'seconds': time.monotonic() - start,
                           'log': name + '.log'})
        return code, output

    def remote(self, name, command, timeout=300):
        marker = 'CAPMGR_EXIT_' + uuid.uuid4().hex + '='
        wrapped = 'sh -c ' + shlex.quote(command)
        wrapped += '; capmgr_status=$?; printf "\\n' + marker + '%s\\n" "$capmgr_status"'
        code, output = self.run(name, [self.args.sdb, '-s', self.args.serial,
                                      'shell', wrapped], timeout)
        matches = re.findall(re.escape(marker) + r'(\d+)', output)
        remote_code = int(matches[0]) if code == 0 and len(matches) == 1 else code or 125
        self.steps[-1]['remote_complete'] = code == 0 and len(matches) == 1
        self.steps[-1]['transport_exit'] = code
        self.steps[-1]['exit'] = remote_code
        return remote_code, output

    def select(self):
        code, output = self.run('devices', [self.args.sdb, 'devices'])
        if code:
            return False
        live = [line.split()[0] for line in output.splitlines()
                if len(line.split()) >= 2 and line.split()[1] == 'device']
        if self.args.serial not in live:
            self.steps.append({'name': 'selection', 'exit': 2,
                               'reason': 'Explicit serial is not an online device'})
            return False
        return True

    def preflight(self):
        if self.args.target == 'host':
            commands = [('os', ['uname', '-srm']), ('compiler', ['c++', '--version']),
                        ('cmake', ['cmake', '--version']), ('rpm', ['rpmbuild', '--version'])]
            for name, command in commands:
                self.run(name, command)
        else:
            if not self.select():
                return
            self.remote('identity', 'id && uname -srm && cat /etc/os-release && df -h /')
            self.remote('compiler', 'g++ --version && gcc -print-prog-name=cc1')
            self.remote('build-tools', 'cmake --version && make --version && rpmbuild --version && diff --version && python3 --version')
            code, output = self.remote('tidlc-version', 'tidlc --version')
            # This installed generator deliberately returns 1 for --version.
            if code == 1 and re.search(r'^tidlc \d+\.\d+', output, re.M):
                self.steps[-1]['expected_exit'] = 1
            self.remote('dependencies', 'pkg-config --modversion sqlite3 gtest gmock')
            self.remote('sqlite-fts', "sqlite3 :memory: 'CREATE VIRTUAL TABLE f USING fts5(text);'")

    def foundation(self):
        if self.args.target == 'host':
            self.run('foundation', ['ctest', '--test-dir', str(self.args.build_dir),
                                    '--output-on-failure', '--no-tests=error'], 300)
        else:
            if not self.select():
                return
            if not self.args.remote_build_dir:
                raise ValueError('--remote-build-dir is required for SDB foundation')
            self.remote('foundation', 'ctest --test-dir ' +
                        shlex.quote(self.args.remote_build_dir) + ' --output-on-failure --no-tests=error')

    def fixture_mode(self):
        names = {'smoke': ['capmgr-c-consumer', 'capmgr-unit-tests'],
                 'integration': ['capmgr-adapter-tests'],
                 'perf': ['capmgr-catalog-benchmark']}[self.args.mode]
        if self.args.target == 'sdb' and not self.select():
            return
        for name in names:
            extra = ['--rows', str(self.args.rows), '--iterations', str(self.args.iterations)] if self.args.mode == 'perf' else []
            if self.args.target == 'host':
                self.run(name, [str((self.args.build_dir / name).resolve()), *extra])
            else:
                command = shlex.join([self.args.remote_bin_dir + '/' + name, *extra])
                self.remote(name, command)

    def native_build(self):
        if self.args.target != 'sdb' or not self.select():
            raise ValueError('native-build requires an online explicitly selected SDB target')
        if not self.args.source_archive or not self.args.json_source:
            raise ValueError('--source-archive and --json-source are required')
        archive_safe(self.args.source_archive, 'capability-manager-0.1.0')
        archive_safe(self.args.json_source, 'json-3.11.3')
        self.inputs = [{'path': str(path.resolve()), 'bytes': path.stat().st_size,
                        'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
                       for path in (self.args.source_archive, self.args.json_source)]
        expected = '0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406'
        if hashlib.sha256(self.args.json_source.read_bytes()).hexdigest() != expected:
            raise ValueError('JSON source must be the pinned nlohmann/json v3.11.3 archive')
        self.remote_root = '/opt/usr/capmgr-validation-' + uuid.uuid4().hex
        root = shlex.quote(self.remote_root)
        if self.remote('create-scope', 'mkdir ' + root + ' && touch ' + root + '/.capmgr-fixture')[0]:
            return
        for name, path in [('source.tar.gz', self.args.source_archive),
                           ('json.tar.gz', self.args.json_source),
                           ('run_bounded.py', Path(__file__).with_name('run_bounded.py'))]:
            if self.run('push-' + name, [self.args.sdb, '-s', self.args.serial,
                        'push', str(path.resolve()), self.remote_root + '/' + name])[0]:
                return
        command = ('cd ' + root + ' && tar xf source.tar.gz && tar xf json.tar.gz'
                   ' && cmake -S capability-manager-0.1.0 -B build'
                   ' -DCMAKE_BUILD_TYPE=Debug -DCAPMGR_REQUIRE_TIDL=ON'
                   ' -DCAPMGR_JSON_SOURCE=' + root + '/json-3.11.3'
                   ' && cmake --build build --target check --parallel 1')
        code, _ = self.remote('native-build', 'python3 ' + root + '/run_bounded.py --seconds 1700 -- sh -c ' + shlex.quote(command), 1800)
        if self.args.cleanup and self.steps[-1].get('remote_complete', False) and code != 124:
            self.remote('cleanup', 'test -f ' + root + '/.capmgr-fixture && rm -rf -- ' + root)

    def finish(self, error=None):
        failed = any(step['exit'] != step.get('expected_exit', 0) for step in self.steps)
        status = 'FAIL' if failed or error else 'PASS'
        report = {'mode': self.args.mode, 'target': self.args.target,
                  'serial': self.args.serial, 'status': status,
                  'coverage': 'isolated adapter/catalog fixtures; not platform integration acceptance',
                  'platform_integration': 'NOT_RUN', 'arm_build': 'NOT_RUN',
                  'steps': self.steps, 'inputs': self.inputs, 'remote_scope': self.remote_root,
                  'error': error}
        (self.output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps({'status': status, 'report': str(self.output / 'summary.json')}))
        return int(status != 'PASS')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['preflight', 'foundation', 'native-build', 'smoke', 'integration', 'perf'])
    parser.add_argument('--target', choices=['host', 'sdb'], required=True)
    parser.add_argument('--serial')
    parser.add_argument('--sdb', default=os.environ.get('SDB', 'sdb'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, default=Path('build'))
    parser.add_argument('--remote-build-dir')
    parser.add_argument('--source-archive', type=Path)
    parser.add_argument('--json-source', type=Path)
    parser.add_argument('--cleanup', action='store_true')
    parser.add_argument('--remote-bin-dir', default='/usr/libexec/capmgr')
    parser.add_argument('--rows', type=int, default=1000)
    parser.add_argument('--iterations', type=int, default=100)
    args = parser.parse_args()
    if args.target == 'sdb' and not args.serial:
        parser.error('--serial is required for SDB; no implicit target selection')
    try:
        runner = Runner(args)
    except (ValueError, OSError) as error:
        print(str(error), file=sys.stderr)
        return 2
    try:
        if args.mode in ('smoke', 'integration', 'perf'):
            runner.fixture_mode()
        else:
            getattr(runner, args.mode.replace('-', '_'))()
        return runner.finish()
    except (ValueError, OSError, tarfile.TarError) as error:
        return runner.finish(str(error))


if __name__ == '__main__':
    sys.exit(main())
