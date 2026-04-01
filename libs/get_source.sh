#!/bin/bash
set -e

# Dependencies (sing-box, sing-quic, libneko) are now managed via go.mod.
# No separate cloning is needed. This script is kept for CI compatibility.

source libs/env_deploy.sh
echo "Using go.mod for dependency management. No source cloning required."
