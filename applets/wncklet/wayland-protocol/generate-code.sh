#!/bin/bash
# This script uses wayland-scanner to generate C bindings from Wayland protocol XML definitions
# It only needs to be called with the XML files change, and is not called as part of the build system

# Fail the script if anything goes wrong
set -euo pipefail

# Get the directory this script is in
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

echo "Looking for Wayland protocols in $SCRIPT_DIR"

# Loop through all XML files in the same directory as this script
for PROTO_FILE_PATH in $(ls "$SCRIPT_DIR"/*.xml); do
	# Strip the path and the .xml extension
	PROTO_NAME=$(basename "$PROTO_FILE_PATH" .xml)
	echo "Generating C bindings for $PROTO_NAME"
	wayland-scanner -c client-header "$PROTO_FILE_PATH" "$SCRIPT_DIR/$PROTO_NAME-client.h"
	wayland-scanner -c private-code "$PROTO_FILE_PATH" "$SCRIPT_DIR/$PROTO_NAME-code.c"
done
