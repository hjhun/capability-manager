#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Prevent renumbering the existing private TIDL wire methods."""
import pathlib
import re
import sys

EXPECTED = {
    "__Result": 0, "__Callback": 1, "AuthorizeCatalog": 2, "Execute": 3,
    "Cancel": 4, "RemountResources": 5, "RegisterReply": 6,
    "UnregisterReply": 7, "RegisterChanged": 8, "UnregisterChanged": 9,
    "ConfirmCatalog": 10,
}
for kind in ("proxy", "stub"):
    source = (pathlib.Path(sys.argv[1]) / f"capability_manager_{kind}.h").read_text()
    enum = re.search(r"enum class MethodId\s*:\s*int\s*\{([^}]+)\}", source)
    if not enum:
        raise SystemExit(f"{kind}: missing method ID enum")
    actual = {name: int(value) for name, value in
              re.findall(r"(\w+)\s*=\s*(\d+)", enum.group(1))}
    if actual != EXPECTED:
        raise SystemExit(f"{kind}: changed TIDL method IDs: {actual!r}")
print("TIDL_METHOD_IDS_PASS existing0..9 unchanged, ConfirmCatalog10")
