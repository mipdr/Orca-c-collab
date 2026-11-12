#!/bin/bash
# Collaboration Feature Testing Script
# This script helps coordinate testing with 3 Orca instances

set -e

ORCA_BIN="./build/orca"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Orca-C Collaboration Testing ===${NC}"
echo ""

# Check if build exists
if [ ! -f "$ORCA_BIN" ]; then
    echo -e "${RED}Error: Orca binary not found at $ORCA_BIN${NC}"
    echo "Run: ./tool build orca"
    exit 1
fi

echo -e "${GREEN}✓ Build found${NC}"
echo ""

# Create test grid
TEST_GRID=$(mktemp /tmp/orca_test_XXXXXX.orca)
cat > "$TEST_GRID" << 'EOF'
........................................
........................................
........................................
........................................
........................................
........................................
........................................
........................................
........................................
........................................
........................................
........................................
........................................
........................................
........................................
........................................
EOF

echo -e "${GREEN}✓ Test grid created: $TEST_GRID${NC}"
echo ""

# Test 1: Check if Orca runs
echo -e "${YELLOW}Test 1: Verify Orca launches${NC}"
timeout 2 "$ORCA_BIN" "$TEST_GRID" > /dev/null 2>&1 || true
if [ $? -eq 124 ]; then
    echo -e "${GREEN}✓ Orca launches successfully${NC}"
else
    echo -e "${RED}✗ Orca failed to launch${NC}"
    exit 1
fi
echo ""

# Instructions for manual testing
echo -e "${YELLOW}=== Manual Testing Instructions ===${NC}"
echo ""
echo "The build is ready. To test collaboration, you need to:"
echo ""
echo -e "${GREEN}Terminal 1 (Server):${NC}"
echo "  $ $ORCA_BIN $TEST_GRID"
echo "  Press Ctrl+D → Collaboration → Start Server → Port: 7777"
echo "  Status should show: SRV:0"
echo ""
echo -e "${GREEN}Terminal 2 (Client 1 - Alice):${NC}"
echo "  $ $ORCA_BIN $TEST_GRID"
echo "  Press Ctrl+D → Collaboration → Join Server"
echo "  Server: localhost:7777"
echo "  Name: Alice"
echo "  Status should show: CLIENT"
echo ""
echo -e "${GREEN}Terminal 3 (Client 2 - Bob):${NC}"
echo "  $ $ORCA_BIN $TEST_GRID"
echo "  Press Ctrl+D → Collaboration → Join Server"
echo "  Server: localhost:7777"
echo "  Name: Bob"
echo "  Status should show: CLIENT"
echo ""
echo -e "${YELLOW}=== Test Checklist ===${NC}"
echo ""
echo "[ ] Server starts and shows SRV:0"
echo "[ ] Alice connects - server shows SRV:1, notification 'Alice joined'"
echo "[ ] Bob connects - server shows SRV:2, notification 'Bob joined'"
echo "[ ] All see each other's cursors in different colors"
echo "[ ] Type on server - clients see it"
echo "[ ] Type on Alice - server and Bob see it"
echo "[ ] Type on Bob - server and Alice see it"
echo "[ ] Server presses spacebar - all start playing"
echo "[ ] Alice tries spacebar - sees 'Playback controlled by server'"
echo "[ ] Server changes BPM - all update"
echo "[ ] Move cursors - all see movements"
echo "[ ] Select area - all see selections"
echo "[ ] Paste on any instance - all see paste"
echo "[ ] Disconnect Alice - notification shown"
echo "[ ] Reconnect Alice - syncs current grid"
echo ""
echo -e "${YELLOW}For automated unit tests, run:${NC}"
echo "  cd tests && ./run_tests.sh"
echo ""
echo -e "${GREEN}Test grid will remain at: $TEST_GRID${NC}"
echo ""
