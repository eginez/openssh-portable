# WSL Agent Bridge Design

## Problem

Mac/Linux clients connecting to Windows sshd with `-A` (agent forwarding) fail because:

1. The SSH agent forwarding protocol sends a **Unix domain socket path** (e.g. `/tmp/ssh-XXXXX/agent.XXXX`) in the `auth-agent-forward@openssh.com` channel open message.
2. Windows sshd cannot create a real Unix domain socket — `AF_UNIX` on Windows only works for WSL interop, not for native Win32 processes.
3. Mac clients expect a Unix socket path and cannot connect to TCP addresses with default OpenSSH.

## Goal

Enable Mac/Linux clients (using default OpenSSH `ssh`) to use agent forwarding with a Windows sshd server, so that keys from the client's local agent can authenticate to remote servers through the Windows session.

## Architecture

```
                    ┌─────────────────────┐
                    │   WSL Agent Bridge  │
                    │   (wsld-agent-      │◄── Creates AF_UNIX listener
                    │    bridge.exe)      │    at /tmp/ssh-XXXXX/agent.XXXX
                    └──────┬──────────┬───┘
                           │          │
              AF_UNIX sock │          │ TCP connect to 127.0.0.1:PORT
                           │          │
             ┌─────────────┘          └─────────────┐
             ▼                                      ▼
     Mac/Linux ssh                         Windows ssh shell
     client (via forwarding                (uses TCP directly
      channel)                             via SSH_AUTH_SOCK)
             │
             ▼
     Windows sshd TCP listener
     (created by session.c fix)
```

## Components

### 1. wsld-agent-bridge.exe

A minimal standalone C executable that bridges a Unix domain socket to a TCP connection.

**CLI:**
```
wsld-agent-bridge.exe <tcp_port> <unix_socket_path>
```

**Behavior:**
1. Parses CLI args for TCP port and Unix socket path
2. Connects to `127.0.0.1:<tcp_port>` via TCP
3. Creates an AF_UNIX listener at `<unix_socket_path>`
4. When a client connects to the Unix socket:
   - Accepts the connection
   - Spawns a forwarding thread (or uses select/poll)
   - Forwards data bidirectionally between Unix socket and TCP connection
5. Exits when the TCP connection closes (clean lifecycle)

**Constraints:**
- No dependency on sshd code or libraries
- Uses only basic socket APIs (works in both WSL and native Windows)
- No authentication — any process that can connect to the Unix socket can use it (same security model as Unix socket agent forwarding)

### 2. session.c Integration

After successfully creating the TCP listener in `auth_input_request_forwarding()`:

1. **On Windows**, after TCP bind+listen succeeds:
   - Create a unique directory in WSL filesystem (e.g. `/tmp/ssh-XXXXXXXXXX` via `mkdtemp` on WSL path)
   - Format the Unix socket path: `WSL_PATH + "/agent." + pid`
   - Spawn `wsld-agent-bridge.exe <port> <unix_path>` as a detached child process
   - Set `SSH_AUTH_SOCK` to the WSL Unix socket path (for Windows shell)
   - Store the TCP address for reference

2. **Error handling**: If the bridge spawn fails, fall back to TCP-only mode (log warning, continue without agent forwarding).

### 3. Build System

- Add `wsld-agent-bridge.vcxproj` to `contrib/win32/openssh/`
- Output: `bin/x64/Debug/wsld-agent-bridge.exe`
- No special dependencies — pure C with Winsock

## Data Flow (Agent Forwarding)

1. Mac client connects to Windows sshd with `-A`
2. Windows sshd creates TCP listener on random port (session.c)
3. Windows sshd spawns WSL agent bridge with TCP port + Unix path
4. Bridge connects to TCP listener and creates Unix socket in WSL
5. Mac client receives Unix socket path in `auth-agent-forward@openssh.com` channel open
6. Mac client connects to Unix socket via WSL filesystem path
7. Bridge forwards data between Unix socket connection and TCP connection to sshd
8. Data flows bidirectionally: Mac agent <---> bridge <---> Windows sshd

## Files Changed

| File | Change |
|------|--------|
| `contrib/win32/openssh/wsld-agent-bridge.c` | New — bridge executable source |
| `contrib/win32/openssh/wsld-agent-bridge.vcxproj` | New — MSBuild project |
| `contrib/win32/openssh/Win32-OpenSSH.sln` | Modified — add bridge project |
| `session.c` | Modified — spawn bridge after TCP bind, set SSH_AUTH_SOCK |
| `authfd.c` | Already modified — TCP address support for custom clients |

## Success Criteria

1. Mac client (`ssh -A -p 2222 user@winhost`) successfully forwards agent
2. Inside Windows session: `ssh-add -l` lists keys from Mac agent
3. Keys can be used to authenticate to remote servers (e.g. `git@github.com`)
4. Windows-native clients (custom ssh.exe) can still use agent forwarding via TCP
5. Bridge process cleans up on session exit

## Risk Assessment

| Risk | Mitigation |
|------|-----------|
| WSL not installed | Fall back to TCP-only mode with clear log message |
| Bridge crashes | Session continues, just agent forwarding fails |
| Security — Unix socket accessible by all | WSL bridge runs with user privileges, socket created with restrictive permissions |
| Performance overhead | Bridge uses non-blocking I/O, single thread per connection |
