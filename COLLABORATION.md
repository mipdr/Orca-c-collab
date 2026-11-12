# Orca Collaboration Guide

Orca supports real-time collaborative editing, allowing multiple users to work on the same grid simultaneously. This guide covers how to set up and use the collaboration features.

## Quick Start

### Starting a Server (Host)

1. Open Orca
2. Press `Ctrl+D` to open the menu
3. Select "Collaboration"
4. Select "Start Server"
5. Enter a port number (default: 7777)
6. Share your IP address and port with collaborators

### Joining a Server (Client)

1. Open Orca
2. Press `Ctrl+D` to open the menu
3. Select "Collaboration"
4. Select "Join Server"
5. Enter `hostname:port` (e.g., `192.168.1.100:7777`)
6. Press Enter to connect

### Disconnecting

1. Press `Ctrl+D` to open the menu
2. Select "Collaboration"
3. Select "Disconnect"

## Features

### Real-Time Grid Sync
- All changes are synchronized instantly across all connected clients
- Single character edits, selections, cut/paste, and delete operations
- Full grid state is sent to new clients when they join

### Multi-Cursor Display
- See where other collaborators are working in real-time
- Each user is assigned a unique color
- Cursor positions update 20 times per second

### Playback Sync
- Play/pause state is synchronized across all clients
- BPM changes are broadcast to all users
- Tick counter stays in sync (with automatic drift correction)

### Join/Leave Notifications
- See when collaborators connect or disconnect
- Notifications appear briefly at the bottom of the screen

### Auto-Resync
- Grid integrity is verified every 5 seconds using checksums
- If corruption is detected, the grid automatically resyncs
- This ensures all clients stay in perfect sync

## Configuration

Collaboration settings are saved to your `orca.conf` file. Available options:

```
# Default port for hosting servers
collab_default_port=7777

# Default server address for joining
collab_default_server=localhost

# Your display name (defaults to $USER)
collab_default_name=YourName

# Show join/leave notifications (true/false)
collab_auto_announce=true

# Hash verification interval in milliseconds (1000-60000)
collab_hash_interval_ms=5000

# Cursor update throttle in milliseconds (10-500)
collab_cursor_throttle_ms=50
```

## Server Setup

### Basic Setup

The simplest setup is to run Orca and start a server. By default:
- Server listens on all network interfaces
- Port 7777 is used (configurable)
- Up to 10 clients can connect simultaneously

### Network Requirements

- TCP port must be accessible (check firewall settings)
- For local network: use your local IP (e.g., 192.168.x.x)
- For internet: port forwarding may be required on your router

### Finding Your IP Address

**Linux/Mac:**
```bash
ip addr show | grep inet    # or
ifconfig | grep inet
```

**For internet access:**
```bash
curl ifconfig.me
```

## Client Connection

### Connection Format

When joining a server, use one of these formats:
- `hostname:port` - e.g., `192.168.1.100:7777`
- `hostname` - uses default port 7777
- `port` - connects to localhost with specified port

### Connection States

After clicking "Join Server":
1. **Connecting** - Establishing TCP connection
2. **Joining** - Sending join request and waiting for acceptance
3. **Syncing** - Receiving full grid from server
4. **Active** - Fully connected and synchronized

## Troubleshooting

### "Connection refused"
- Server may not be running
- Check the port number is correct
- Verify server firewall allows incoming connections

### "Connection timed out"
- Network connectivity issue
- Server may be unreachable
- Check if you can ping the server

### "Port already in use"
- Another application (or another Orca instance) is using the port
- Try a different port number
- Check for zombie processes: `lsof -i :7777`

### "Permission denied" (ports < 1024)
- Ports below 1024 require administrator privileges
- Use a port number >= 1024 (7777 is recommended)

### Grid Out of Sync
- Normally auto-corrects within 5 seconds
- If persistent, try disconnecting and reconnecting
- Server's grid is authoritative - clients resync to server

### High Latency
- Normal for remote servers over the internet
- Edits appear locally immediately, then sync
- Consider using a server geographically closer to all participants

### Client Disconnects Unexpectedly
- Check network stability
- Server may have crashed or been stopped
- Try reconnecting

## Known Limitations

### Current Limitations
- Maximum 10 simultaneous clients
- No authentication or authorization
- No encryption (data sent in plaintext)
- No conflict resolution - last write wins
- No undo sync (each client has independent undo history)
- No support for grid resize during session

### Security Warning
**The collaboration feature has no built-in security.** Anyone who can connect to your server can:
- See and modify the entire grid
- See other users' cursor positions
- See your display name

**Recommendations:**
- Only use on trusted networks
- Use a VPN for internet collaboration
- Don't expose to the public internet without a firewall

## Protocol Overview

For developers interested in the protocol:

- **Transport**: TCP with framed messages
- **Message Format**: 4-byte header (magic + type + length) + payload
- **Sync Method**: Full grid sync on join, incremental writes after
- **Integrity**: xxHash64 checksums verified periodically
- **Encoding**: Network byte order (big-endian)

See `docs/COLLABORATION_DESIGN.md` for full technical details.

## Tips for Effective Collaboration

1. **Assign areas**: Different users work in different grid regions
2. **Use BPM strategically**: Lower BPM gives more time to see changes
3. **Communicate**: Use voice/text chat alongside Orca
4. **Save often**: The server's grid is not automatically saved to disk
5. **One conductor**: Let one person control play/pause to avoid conflicts

## Command Reference

| Action | Menu Path |
|--------|-----------|
| Start Server | Ctrl+D -> Collaboration -> Start Server |
| Join Server | Ctrl+D -> Collaboration -> Join Server |
| Disconnect | Ctrl+D -> Collaboration -> Disconnect |

## Feedback and Issues

If you encounter bugs or have suggestions for the collaboration features, please open an issue on the project's GitHub repository.
