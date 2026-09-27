#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
output_dir=${1:?usage: generate.sh OUTPUT_DIRECTORY}
mkdir -p "$output_dir"
"${TIDLC:-tidlc}" -p -l C++ -i "$source_dir/capability_manager.tidl" -o "$output_dir/capability_manager_proxy"
"${TIDLC:-tidlc}" -s -e -l C++ -i "$source_dir/capability_manager.tidl" -o "$output_dir/capability_manager_stub"
"${PYTHON3:-python3}" "$source_dir/bind_channels.py" "$output_dir"
