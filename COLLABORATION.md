# Orca-C Collaboration Guide

**Real-time collaborative music coding!** 🎵✨

Orca-C now supports real-time collaborative editing, allowing up to 10 people to work on the same grid simultaneously. See each other's cursors, sync playback, and create music together!

---

## 🚀 Quick Start

### Option 1: Starting a Server (Host)

**The server is the "host" - they control playback and hold the source-of-truth grid.**

1. Launch Orca: `./build/orca` (or just `orca` if installed)
2. Press `Ctrl+D` to open the menu
3. Navigate to `Collaboration` → `Start Server...`
4. Enter a port number (press Enter for default: `7777`)
5. You'll see **`SRV:0`** in the status bar (green) - server is running!
6. Share your IP address and port with collaborators

**Finding your IP address:**
```bash
# Local network (LAN)
ip addr show | grep inet    # Linux
ifconfig | grep inet        # macOS

# For internet collaboration
curl ifconfig.me
```

### Option 2: Joining a Server (Client)

**Clients can edit the grid but cannot control playback (play/pause/BPM).**

1. Launch Orca: `./build/orca`
2. Press `Ctrl+D` to open the menu
3. Navigate to `Collaboration` → `Join Server...`
4. Enter server address in one of these formats:
   - `192.168.1.100:7777` (IP address with port)
   - `localhost:7777` (local server)
   - `192.168.1.100` (uses default port 7777)
5. Enter your display name (e.g., "Alice")
6. Press Enter - you'll see **`CLIENT`** in the status bar (cyan)
7. Grid will sync, and you're ready to collaborate!

### Disconnecting

- Press `Ctrl+D` → `Collaboration` → `Disconnect`
- Or just quit Orca (it disconnects automatically)

---

## 🎨 Example Session

Here's a complete walkthrough of a 3-person jam session:

### Step 1: Alice starts the server

```bash
# Terminal 1 - Alice (Server)
$ ./build/orca
[Opens menu with Ctrl+D]
Collaboration → Start Server... → Port: 7777
Status bar shows: SRV:0 (0 clients connected)
Alice's IP: 192.168.1.100
```

### Step 2: Bob joins as client

```bash
# Terminal 2 - Bob (Client)
$ ./build/orca
[Opens menu with Ctrl+D]
Collaboration → Join Server...
Server: 192.168.1.100:7777
Name: Bob
[Connects]
Status bar shows: CLIENT
```

Alice now sees: **`SRV:1`** (1 client connected)
Notification appears: **"Bob joined"** 🟡

### Step 3: Carol joins as client

```bash
# Terminal 3 - Carol (Client)
$ ./build/orca
[Opens menu with Ctrl+D]
Collaboration → Join Server...
Server: 192.168.1.100:7777
Name: Carol
[Connects]
Status bar shows: CLIENT
```

Alice now sees: **`SRV:2`** (2 clients connected)
Notification appears: **"Carol joined"** 🔵

### Step 4: Everyone collaborates!

- **Alice (server)** sees Bob's yellow cursor and Carol's blue cursor
- **Bob** sees Alice's green cursor and Carol's blue cursor
- **Carol** sees Alice's green cursor and Bob's yellow cursor
- Everyone types and edits - changes appear instantly for all
- Alice presses **spacebar** → everyone starts playing together
- Alice changes **BPM** → everyone's tempo updates
- Someone disconnects → notification shows **"Bob left"**

---

## ✨ Features

### 🔄 Real-Time Grid Sync
- **Instant updates**: See edits as they happen
- **All operations sync**: Typing, cut, paste, fill, slide
- **New joiners get full state**: Late arrivals see the current grid

### 👥 Multi-Cursor Display
- **10 unique colors**: Each person has a distinct cursor color
- **See selections**: Rectangular selections show in cursor color
- **20 updates/second**: Smooth cursor movement
- **Auto-cleanup**: Inactive cursors fade after 10 seconds

### 🎵 Synchronized Playback
- **Server controls**: Only the host can play/pause
- **BPM sync**: Tempo changes instantly across all clients
- **Tick sync**: Everyone hears the exact same timing
- **Client restriction**: Clients see "Playback controlled by server" if they try to play/pause

### 📢 Notifications
- **Join/leave alerts**: "Alice joined" / "Bob left"
- **Auto-dismiss**: Notifications disappear after 3 seconds
- **Color-coded**: Match user's cursor color

### 🔐 Auto-Resync Protection
- **Hash verification**: Every 5 seconds, grids are checksummed
- **Auto-recovery**: If corruption detected, instant resync
- **Invisible**: Happens in background, you won't notice

## ⚙️ Configuration

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

## 🔧 Troubleshooting

### Connection Issues

| Problem | Solution |
|---------|----------|
| **"Connection refused"** | • Server may not be running<br>• Check port number is correct<br>• Check firewall allows incoming connections |
| **"Connection timed out"** | • Check network connectivity<br>• Verify server is reachable (`ping` the IP)<br>• Check router/firewall settings |
| **"Port already in use"** | • Another app is using the port<br>• Try a different port (e.g., 7778)<br>• Kill zombie process: `lsof -i :7777` then `kill <PID>` |
| **"Permission denied"** | • Ports < 1024 need root access<br>• Use port ≥ 1024 (recommended: 7777) |

### Sync Issues

| Problem | Solution |
|---------|----------|
| **Grid out of sync** | • Wait 5 seconds for auto-resync<br>• If persistent, disconnect and rejoin<br>• Server grid is authoritative |
| **High latency** | • Normal over internet<br>• Edits are optimistic (instant locally)<br>• Use a server closer geographically |
| **Client disconnects** | • Check network stability<br>• Server may have crashed<br>• Reconnect and continue |

### Playback Issues

| Problem | Solution |
|---------|----------|
| **Can't play/pause (client)** | • This is normal - only server controls playback<br>• Ask the server host to press spacebar |
| **Can't change BPM (client)** | • This is normal - only server controls BPM<br>• Ask the server host to change tempo |
| **Playback desyncs** | • Auto-corrects every 500ms<br>• If persistent, restart the session |

## ⚠️ Known Limitations

### What Works
✅ Up to 10 simultaneous clients
✅ Real-time grid sync
✅ Multi-cursor display
✅ Synchronized playback
✅ Auto-resync on corruption
✅ All edit operations (type, cut, paste, fill, slide)

### What Doesn't Work (Yet)
❌ Authentication/passwords
❌ Encryption (data sent in plaintext)
❌ Undo synchronization (each client has independent undo)
❌ Grid resize during session
❌ NAT traversal (need port forwarding for internet)
❌ Conflict resolution (last write wins)

### 🔒 Security Warning

**⚠️ IMPORTANT: The collaboration feature has NO security.**

Anyone who can connect to your server can:
- ✏️ See and modify the entire grid
- 👁️ See all users' cursor positions
- 📝 See your display name

**Safe to use:**
- ✅ Local network (LAN) with trusted people
- ✅ VPN with trusted collaborators
- ✅ Localhost (testing)

**NOT safe to use:**
- ❌ Public internet without VPN
- ❌ Untrusted networks
- ❌ With sensitive/private content

**Recommendations:**
1. **Use a VPN** for internet collaboration (Tailscale, WireGuard, etc.)
2. **Firewall rules** - only allow specific IPs if exposed to internet
3. **Trusted networks only** - don't expose to public internet
4. **Save frequently** - server crashes lose all unsaved work

## Protocol Overview

For developers interested in the protocol:

- **Transport**: TCP with framed messages
- **Message Format**: 4-byte header (magic + type + length) + payload
- **Sync Method**: Full grid sync on join, incremental writes after
- **Integrity**: xxHash64 checksums verified periodically
- **Encoding**: Network byte order (big-endian)

See `docs/COLLABORATION_DESIGN.md` for full technical details.

## 💡 Tips for Effective Collaboration

### Best Practices
1. **🗺️ Assign areas** - Different users work in different grid regions to minimize conflicts
2. **🎼 Lower BPM** - Slower tempo gives everyone more time to see changes
3. **💬 External communication** - Use voice/text chat alongside Orca (Discord, Zoom, etc.)
4. **💾 Save often** - Server's grid is NOT auto-saved. Save frequently!
5. **👨‍✈️ One conductor** - Designate the server host as the "conductor" for play/pause
6. **🎨 Color coordination** - Use cursor colors to identify who's working where
7. **⏸️ Pause before big changes** - Ask everyone to pause before major grid restructuring

### Common Workflows

**🎹 Live Jamming:**
- Server (host) controls playback and tempo
- Clients add/modify operators in their assigned regions
- Everyone hears the same output in real-time

**📝 Collaborative Composition:**
- Work on different sections (intro, verse, chorus)
- Lower BPM for easier editing
- Use voice chat to coordinate changes

**🎓 Teaching/Learning:**
- Teacher hosts the server
- Students connect as clients
- Teacher demonstrates, students follow along
- Students can experiment without affecting others' work

---

## 📋 Quick Reference

### Menu Commands

| Action | Path |
|--------|------|
| Start Server | `Ctrl+D` → `Collaboration` → `Start Server...` |
| Join Server | `Ctrl+D` → `Collaboration` → `Join Server...` |
| Disconnect | `Ctrl+D` → `Collaboration` → `Disconnect` |

### Server Formats

```
192.168.1.100:7777   # IP with custom port
192.168.1.100        # IP with default port (7777)
localhost:7777       # Local server with custom port
localhost            # Local server with default port
```

### Status Indicators

| Indicator | Meaning |
|-----------|---------|
| `SRV:0` (green) | Server running, 0 clients connected |
| `SRV:3` (green) | Server running, 3 clients connected |
| `CLIENT` (cyan) | Connected to server as client |

---

## 🐛 Feedback and Issues

Found a bug? Have a feature request?

Please open an issue on the GitHub repository: **[mipdr/Orca-c-collab](https://github.com/mipdr/Orca-c-collab/issues)**

Include:
- What you were doing
- What you expected to happen
- What actually happened
- Any error messages
- Your OS and Orca version

---

## 📚 Further Reading

- **Technical Design**: See `docs/COLLABORATION_DESIGN.md` for protocol details
- **Orca Manual**: Press `?` in Orca for operator reference
- **Original Orca**: https://github.com/hundredrabbits/Orca

---

**Happy collaborative music making! 🎵✨**
