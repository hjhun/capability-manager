# SPDX-License-Identifier: Apache-2.0
"""Separate-process offline transactions; no AMD, TIDL or live package manager."""
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import unittest

BINARY = str(Path(sys.argv.pop(1)).resolve())


class OfflinePackageTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='capmgr-offline-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.db = self.root / 'catalog.db'
        (self.root / 'res/skills/example').mkdir(parents=True)
        (self.root / 'res/skills/example/SKILL.md').write_text('# Fixture\n')
        self.descriptor = dict(version=1, key='example', name='Example',
                               desc='Find pictures', resource='res/skills/example')
        self.write_descriptor()
        self.manifest = dict(version=1, operation='install-1', owner='fixture.pkg',
                             mode='replace', root=str(self.root), metadata=[dict(
            key='http://tizen.org/metadata/capability/skill', value='skill.json')])

    def write_descriptor(self):
        (self.root / 'skill.json').write_text(json.dumps(self.descriptor))

    def invoke(self, *args, ok=True):
        result = subprocess.run([BINARY, '--offline', str(self.db), *args],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0 if ok else 1, result.stderr)
        return json.loads(result.stdout if ok else result.stderr)

    def stage(self, ok=True):
        path = self.root / 'manifest.json'
        path.write_text(json.dumps(self.manifest))
        return self.invoke('stage', str(path), ok=ok)

    def query(self, sql):
        with sqlite3.connect(f'file:{self.db}?mode=ro', uri=True) as connection:
            return connection.execute(sql).fetchall()

    def install(self):
        self.stage()
        self.invoke('finalize', 'install-1', 'success')

    def test_pending_survives_process_exit_and_replay_never_publishes(self):
        self.assertEqual(self.stage()['revision'], 0)
        self.assertEqual(self.stage()['revision'], 0)
        self.assertEqual(self.invoke('status', 'install-1')['state'], 'pending')
        self.assertEqual(self.query('SELECT id FROM capability'), [])
        self.assertEqual(self.query('SELECT rowid FROM capability_fts'), [])
        self.assertEqual(self.invoke('finalize', 'install-1', 'success')['revision'], 1)
        self.assertEqual(self.invoke('finalize', 'install-1', 'success')['revision'], 1)
        self.assertEqual(self.query('SELECT id FROM capability'), [('skill:example',)])
        self.assertEqual(len(self.query('SELECT rowid FROM capability_fts')), 1)
        self.assertEqual(self.invoke('finalize', 'install-1', 'failure', ok=False)['error']['code'], -5)

    def test_failed_update_preserves_previous_catalog_and_fts(self):
        self.install()
        self.descriptor['name'] = 'Changed'
        self.write_descriptor()
        self.manifest['operation'] = 'update-2'
        self.stage()
        self.assertEqual(self.query('SELECT name FROM capability'), [('Example',)])
        self.assertEqual(self.invoke('finalize', 'update-2', 'failure')['revision'], 1)
        self.assertEqual(self.query('SELECT name FROM capability'), [('Example',)])
        self.manifest['operation'] = 'update-3'
        self.stage()
        self.assertEqual(self.invoke('finalize', 'update-3', 'success')['revision'], 2)
        self.assertEqual(self.query('SELECT name FROM capability'), [('Changed',)])

    def test_remove_without_resources_waits_for_explicit_outcome(self):
        self.install()
        (self.root / 'skill.json').unlink()
        self.manifest = dict(version=1, operation='remove-2', owner='fixture.pkg', mode='remove')
        self.stage()
        self.assertEqual(len(self.query('SELECT id FROM capability')), 1)
        self.invoke('finalize', 'remove-2', 'failure')
        self.assertEqual(len(self.query('SELECT id FROM capability')), 1)
        self.manifest['operation'] = 'remove-3'
        self.stage()
        self.invoke('finalize', 'remove-3', 'success')
        self.assertEqual(self.query('SELECT id FROM capability'), [])
        self.assertEqual(self.query('SELECT rowid FROM capability_fts'), [])

    def test_pending_and_published_other_owner_conflicts(self):
        self.stage()
        self.manifest.update(operation='foreign-2', owner='foreign.pkg')
        self.assertEqual(self.stage(ok=False)['error']['code'], -5)
        self.invoke('finalize', 'install-1', 'success')
        self.assertEqual(self.stage(ok=False)['error']['code'], -5)
        self.assertEqual(self.query('SELECT count(*) FROM pending'), [(0,)])

    def test_mixed_keys_and_app_scope(self):
        skill = self.manifest['metadata'][0]
        skill['value'] = 'skill.json;skill.json'
        self.manifest['metadata'] += [dict(skill), dict(
            key='http://tizen.org/metadata/capability/app-skill', value='skill.json', appId='app.one'), dict(
            key='http://tizen.org/metadata/capability/app-skill', value='skill.json', appId='app.two')]
        self.install()
        self.assertEqual(self.query('SELECT count(*) FROM capability'), [(3,)])

    def test_bad_inputs_do_not_create_database(self):
        self.manifest['metadata'].append(dict(
            key='http://tizen.org/metadata/capability/skill', value='missing.json'))
        self.stage(ok=False)
        self.assertFalse(self.db.exists())
        self.manifest['metadata'] = [dict(key='action', value='skill.json')]
        self.stage(ok=False)
        self.assertFalse(self.db.exists())
        path = self.root / 'invalid.json'
        for data in ['{"version":1,"version":1}', '[' * 40 + ']' * 40, 'x' * (1024 * 1024 + 1)]:
            path.write_text(data)
            self.invoke('stage', str(path), ok=False)
            self.assertFalse(self.db.exists())
        self.invoke('finalize', 'unknown', 'success', ok=False)
        self.assertFalse(self.db.exists())

    def test_offline_flag_and_explicit_outcome_required(self):
        result = subprocess.run([BINARY, str(self.db), 'stage', 'ignored'], capture_output=True)
        self.assertEqual(result.returncode, 2)
        self.assertFalse(self.db.exists())
        self.stage()
        self.invoke('finalize', 'install-1', 'guess', ok=False)
        self.assertEqual(self.invoke('status', 'install-1')['state'], 'pending')


if __name__ == '__main__':
    unittest.main()
