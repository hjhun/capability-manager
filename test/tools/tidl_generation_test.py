# SPDX-License-Identifier: Apache-2.0
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which('tidlc'), 'tidlc unavailable on this host')
class TidlGenerationTests(unittest.TestCase):
    def test_generated_dispatch_checks_main_before_parcel(self):
        with tempfile.TemporaryDirectory(prefix='capmgr-tidl-') as output:
            subprocess.run(['sh', str(ROOT / 'tidl/generate.sh'), output], check=True,
                           capture_output=True, timeout=20)
            source = (pathlib.Path(output) / 'capability_manager_stub.cc').read_text()
            self.assertLess(source.index('RPC_PORT_PORT_MAIN'), source.index('s->OnCreate();'))
            self.assertLess(source.index('b->ValidateChannels(main_fd, callback_fd)'),
                            source.index('rpc_port_parcel_create_from_port(&p, port)'))
            self.assertIn('rpc_port_get_read_fd(port, &main_fd) != 0', source)
            self.assertIn('rpc_port_get_read_fd(callback_port, &callback_fd) != 0', source)
            connect = source[source.index('::OnConnectedCB('):source.index('::OnDisconnectedCB(')]
            self.assertIn('shutdown(main_fd, SHUT_RDWR)', connect)
            self.assertNotIn('rpc_port_disconnect(', connect)

    def test_unknown_generator_layout_fails_without_partial_outputs(self):
        with tempfile.TemporaryDirectory(prefix='capmgr-tidl-') as output:
            output = pathlib.Path(output)
            subprocess.run(['tidlc', '-s', '-e', '-l', 'C++', '-i',
                            str(ROOT / 'tidl/capability_manager.tidl'), '-o',
                            str(output / 'capability_manager_stub')], check=True,
                           capture_output=True, timeout=20)
            header = output / 'capability_manager_stub.h'
            source = output / 'capability_manager_stub.cc'
            original_header = header.read_bytes()
            unsupported = source.read_text().replace('  s->SetPort(port);', '  /* unknown binding */')
            source.write_text(unsupported)
            result = subprocess.run([sys.executable, str(ROOT / 'tidl/bind_channels.py'), str(output)],
                                    capture_output=True, timeout=20)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(header.read_bytes(), original_header)
            self.assertEqual(source.read_text(), unsupported)
