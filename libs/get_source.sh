#!/bin/bash
set -e

# sing-box and sing-quic come through go.mod. libneko does NOT: go.mod carries a
# `replace` directive pointing at ../../../../libneko, so the repository has to be
# cloned next to this one or `go build` fails on a missing path. CI does that in a
# separate step; this script is kept for compatibility.

source libs/env_deploy.sh
echo "Using go.mod for dependency management. No source cloning required."
