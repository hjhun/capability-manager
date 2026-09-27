#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Fail-closed local integration of generated TIDL with both channel identities.

Generated serialization stays owned by tidlc. Exact single anchors are checked
before either output is written; an unknown generator layout fails the build.
"""
import pathlib
import re
import sys


def once(text, anchor, replacement):
    if text.count(anchor) != 1:
        raise ValueError(f"unsupported TIDL layout: expected one {anchor!r}")
    return text.replace(anchor, replacement, 1)


def bind(header, source):
    header = once(header, "#pragma once", '#pragma once\n#include "platform/tidl_channels.hh"')
    header = once(header, "class ServiceBase {", "class ServiceBase : public capmgr::TidlChannels {")
    header = once(header,
                  "      virtual std::unique_ptr<ServiceBase> CreateService(std::string sender, std::string instance) = 0;",
                  """      // Observation only: borrowed stub remains owned by CapabilityManager.
      virtual void OnRejectedConnection(rpc_port_stub_h, const std::string&) noexcept {}
      virtual std::unique_ptr<ServiceBase> CreateService(std::string sender, std::string instance) = 0;""")
    includes = re.findall(r'^#include "[^"\n]*capability_manager_stub\.h"$', source, re.MULTILINE)
    if len(includes) != 1:
        raise ValueError("unsupported TIDL header include")
    source = once(source, includes[0],
                  '#include "capability_manager_stub.h"\n#include <rpc-port-internal.h>\n#include <sys/socket.h>')
    source = once(source, "  s->SetPort(port);", """  rpc_port_h main_port = nullptr;
  int main_fd = -1, callback_fd = -1;
  const int callback_status = rpc_port_get_read_fd(port, &callback_fd);
  const int main_status = rpc_port_stub_get_port(stub->stub_, RPC_PORT_PORT_MAIN, instance, &main_port);
  const int main_fd_status = main_port ? rpc_port_get_read_fd(main_port, &main_fd) : -1;
  if (main_status != 0 || main_fd_status != 0 || callback_status != 0 ||
      !s->BindChannels(main_fd, callback_fd)) {
    // Do not disconnect/close borrowed Port handles here: that removes their
    // watchers before Stub can remove its retained instance entries. Shutdown
    // leaves watchers alive to deliver HUP and run RemoveAcceptedPorts.
    if (main_fd >= 0) shutdown(main_fd, SHUT_RDWR);
    if (callback_fd >= 0) shutdown(callback_fd, SHUT_RDWR);
    stub->service_factory_->OnRejectedConnection(stub->stub_, instance);
    return;
  }
  s->SetPort(port);""")
    source = once(source, "  rpc_port_h callback_port;", "  rpc_port_h callback_port = nullptr;")
    source = once(source, '    _E("Failed to get callback port");',
                  '    _E("Failed to get callback port");\n    rpc_port_disconnect(port);\n    return -1;')
    source = once(source, "  ret = rpc_port_parcel_create_from_port(&p, port);", """  int main_fd = -1, callback_fd = -1;
  if (rpc_port_get_read_fd(port, &main_fd) != 0 ||
      rpc_port_get_read_fd(callback_port, &callback_fd) != 0 ||
      !b->ValidateChannels(main_fd, callback_fd)) {
    rpc_port_disconnect(port);
    return -1;
  }
  ret = rpc_port_parcel_create_from_port(&p, port);""")
    return header, source


def main():
    root = pathlib.Path(sys.argv[1])
    header = root / "capability_manager_stub.h"
    source = root / "capability_manager_stub.cc"
    new_header, new_source = bind(header.read_text(), source.read_text())
    header.write_text(new_header)
    source.write_text(new_source)


if __name__ == "__main__":
    main()
