#!/bin/bash
# Run all collaboration tests

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build/tests"

mkdir -p "$BUILD_DIR"

CC="${CC:-gcc}"
CFLAGS="-std=c99 -Wall -Wextra -g -I$PROJECT_DIR -I$PROJECT_DIR/thirdparty"

echo "Building tests..."
echo ""

# Build test_collab_util
echo "Compiling test_collab_util..."
$CC $CFLAGS -o "$BUILD_DIR/test_collab_util" \
    "$SCRIPT_DIR/test_collab_util.c" \
    "$PROJECT_DIR/collaboration/collab_util.c"

# Build test_collab_protocol
echo "Compiling test_collab_protocol..."
$CC $CFLAGS -o "$BUILD_DIR/test_collab_protocol" \
    "$SCRIPT_DIR/test_collab_protocol.c" \
    "$PROJECT_DIR/collaboration/collab_protocol.c" \
    "$PROJECT_DIR/collaboration/collab_util.c"

# Build test_collab_net
echo "Compiling test_collab_net..."
$CC $CFLAGS -o "$BUILD_DIR/test_collab_net" \
    "$SCRIPT_DIR/test_collab_net.c" \
    "$PROJECT_DIR/collaboration/collab_net.c" \
    "$PROJECT_DIR/collaboration/collab_protocol.c" \
    "$PROJECT_DIR/collaboration/collab_util.c"

# Build test_server
echo "Compiling test_server..."
$CC $CFLAGS -o "$BUILD_DIR/test_server" \
    "$SCRIPT_DIR/test_server.c" \
    "$PROJECT_DIR/collaboration/collab_net.c" \
    "$PROJECT_DIR/collaboration/collab_protocol.c" \
    "$PROJECT_DIR/collaboration/collab_util.c"

# Build test_client
echo "Compiling test_client..."
$CC $CFLAGS -o "$BUILD_DIR/test_client" \
    "$SCRIPT_DIR/test_client.c" \
    "$PROJECT_DIR/collaboration/collab_net.c" \
    "$PROJECT_DIR/collaboration/collab_protocol.c" \
    "$PROJECT_DIR/collaboration/collab_util.c"

echo ""
echo "Running tests..."
echo ""

FAILED=0

echo "========================================"
echo "Running test_collab_util"
echo "========================================"
if "$BUILD_DIR/test_collab_util"; then
    echo "test_collab_util: PASSED"
else
    echo "test_collab_util: FAILED"
    FAILED=1
fi
echo ""

echo "========================================"
echo "Running test_collab_protocol"
echo "========================================"
if "$BUILD_DIR/test_collab_protocol"; then
    echo "test_collab_protocol: PASSED"
else
    echo "test_collab_protocol: FAILED"
    FAILED=1
fi
echo ""

echo "========================================"
echo "Running test_collab_net"
echo "========================================"
if "$BUILD_DIR/test_collab_net"; then
    echo "test_collab_net: PASSED"
else
    echo "test_collab_net: FAILED"
    FAILED=1
fi
echo ""

if [ $FAILED -eq 0 ]; then
    echo "========================================"
    echo "All tests passed!"
    echo "========================================"
    exit 0
else
    echo "========================================"
    echo "Some tests failed!"
    echo "========================================"
    exit 1
fi
