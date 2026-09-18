# CnCNet tunnel server wire protocol

A specification for writing a plain-C client. Every claim below is drawn from source
that was cloned and read, not from recollection. Anything not established by source is
labelled UNVERIFIED.

## 0. Sources

Three repositories were cloned and read in full for the relevant files.

| Short name | Repository | Commit read | Date | What it is |
|---|---|---|---|---|
| **LEGACY-SERVER** | https://github.com/CnCNet/cncnet-server | `3c2a5e2f3aaabf3d0a3e82a133563a269fd7c9c0` (master, only branch, no tags) | code from `5571066`, 2020-08-23 | The GPL-3 C# server named in the requirement. Last code change 2020. |
| **MODERN-SERVER** | https://github.com/Rans4ckeR/cncnet-server | `f3b2ce05ee1f682a31d4dae2bbd4d20ba896a2d1` | 2026-05-08 | .NET 10 rewrite. This is what https://github.com/CnCNet/cncnet-docker-dotnetcore-tunnel packages, and its option names (`--tunnel-port`, `--max-clients-per-ip`) match that Docker repo's README. Treat as the production server. |
| **CLIENT** | https://github.com/CnCNet/xna-cncnet-client | `90eebc7af92e65c032089b0e299e1d9e2d91d97a` (master) | 2026-09-07 | The official CnCNet client. Its V3 support was added in one commit, `cd55155` "Add support for V3 tunnels (#845)", 2026-08-25. |

Important framing before anything else:

- The **tunnel server protocol** is old and stable (2020).
- The **client-to-client conventions layered on top of it** (the `CNCNET` magic bytes, the
  packet-type enum, the negotiation handshake) are two weeks old at the time of writing and
  are entirely a client-side invention. The server has never heard of them. You are free to
  ignore all of it.

A live fetch of `https://cncnet.org/master-list` was also performed to confirm the master
list format against real data.

---

## 1. TUNNEL V3

### 1.1 Is there an HTTP step?

**No. V3 is UDP only. There is no HTTP request, no session allocation, no join.**

This is verifiable by absence and by structure:

- LEGACY-SERVER `TunnelV2.cs:36,56,66-67` constructs an `HttpListener` and binds
  `http://*:<port>/`. `TunnelV3.cs` has no `HttpListener`, no `System.Net.HttpListener`
  import, and no code path that serves HTTP. Its only socket is the `UdpClient` at
  `TunnelV3.cs:141`.
- MODERN-SERVER `TunnelV2.cs:23-48` (`StartHttpServerAsync`) maps `/maintenance`, `/status`
  and `/request`. `TunnelV3.cs` has no equivalent. `CnCNetBackgroundService.cs:78-88` starts
  `tunnelV2.StartHttpServerAsync` only for V2; V3 gets `StartAsync` alone.
- CLIENT `V3TunnelCommunicator.cs:491-516` (`InitializeConnection`) opens one `UdpClient`
  and a receive thread. There is no HTTP anywhere in the V3 path.

A V3 client is registered **implicitly, by the first UDP packet it sends**. See 1.4.

The only HTTP a V3 client performs is fetching the tunnel server list from CnCNet's master
server, which is a separate service (section 3), and is optional if you hard-code a server.

### 1.2 UDP header layout

Exactly 8 bytes, then the payload.

```
offset  size  field        encoding
------  ----  -----------  ----------------------------------------
0       4     sender_id    uint32, LITTLE-ENDIAN
4       4     receiver_id  uint32, LITTLE-ENDIAN
8       N     payload      opaque, forwarded verbatim
```

Sources:

- LEGACY-SERVER `TunnelV3.cs:158-159`:
  ```csharp
  uint senderId = BitConverter.ToUInt32(buffer, 0);
  uint receiverId = BitConverter.ToUInt32(buffer, 4);
  ```
- MODERN-SERVER `TunnelV3.cs:10` (`PlayerIdSize = sizeof(int)`) and `TunnelV3.cs:50-56`:
  ```csharp
  uint senderId = BitConverter.ToUInt32(buffer[..PlayerIdSize].Span);
  uint receiverId = BitConverter.ToUInt32(buffer[PlayerIdSize..(PlayerIdSize * 2)].Span);
  ```
- CLIENT `V3TunnelCommunicator.cs:291-292` (write) and `631-632` (read):
  ```csharp
  BinaryPrimitives.WriteUInt32LittleEndian(span, senderId);
  BinaryPrimitives.WriteUInt32LittleEndian(span[4..], receiverId);
  ```

**Endianness caveat, stated plainly.** Both servers use `BitConverter.ToUInt32`, which is
*host* byte order, not an explicit little-endian read. The servers are therefore technically
endianness-dependent, and only little-endian because every machine they run on is
little-endian. The client, by contrast, is explicitly little-endian
(`BinaryPrimitives.WriteUInt32LittleEndian`). Contrast V2, where the server *does* convert
explicitly (`IPAddress.NetworkToHostOrder`, section 2).

In practice this does not bite us for two reasons: the byte order only has to be *consistent*
between our two clients (the server treats the 8 bytes as an opaque dictionary key), and the
two reserved values, `0` and `0xFFFFFFFF`, are byte-order symmetric. **Write little-endian.**
That matches the client, matches every real server, and matches how CnCNet IDs are printed
in logs.

**The header is not rewritten.** The server forwards the whole datagram byte for byte,
including the original `sender_id`, to the receiver's registered address
(LEGACY-SERVER `TunnelV3.cs:217`, MODERN-SERVER `TunnelV3.cs:126`). So the receiving peer
reads `sender_id` from the packet to know who sent it. There is no separate demux needed.

### 1.3 Reserved IDs and validation rules

Order of checks in the server, and what a client must avoid.

| Condition | Server behaviour |
|---|---|
| `sender_id == 0 && receiver_id == 0xFFFFFFFF && len >= 29` | Treated as a maintenance command. Do not send. |
| `sender_id == 0 && receiver_id != 0` | Dropped. |
| `sender_id == 0 && receiver_id == 0 && len == 50` (exactly) | Ping. Server echoes the first 12 bytes back. See 1.7. |
| `sender_id == 0 && receiver_id == 0 && len != 50` | Dropped. |
| `sender_id == receiver_id` (and non-zero) | Dropped. |
| Source is loopback / `0.0.0.0` / `255.255.255.255` / source port 0 | Dropped. |
| anything else | Relayed (see 1.5). |

Sources: LEGACY-SERVER `TunnelV3.cs:161-187`; MODERN-SERVER `TunnelV3.cs:58-82`
(`ValidateClientIds`) and `Tunnel.cs:172-219` (`HandlePingRequestAsync`).

**Rules for our client:** pick `sender_id` uniformly at random in `1 .. 0xFFFFFFFE`, never
0, never `0xFFFFFFFF`, and never equal to any peer's ID.

### 1.4 Registration: how a client joins

There is no join. The server creates the mapping the first time it sees a packet whose
`sender_id` is not already mapped.

LEGACY-SERVER `TunnelV3.cs:203-213`:
```csharp
else
{
    if (Mappings.Count >= MaxClients || MaintenanceModeEnabled || !NewConnectionAllowed(remoteEP.Address))
        return;

    sender = new TunnelClient();
    sender.RemoteEP = new IPEndPoint(remoteEP.Address, remoteEP.Port);
    sender.SetLastReceiveTick();

    Mappings.Add(senderId, sender);
}
```

MODERN-SERVER `TunnelV3.cs:129-151` (`HandleNewClient`) is the same logic in the newer style.

The mapping is `sender_id -> source IP:port`. That is the entire state the server keeps
about a client.

The CLIENT sends a dedicated 8-byte "register" packet, which is simply a header with
`receiver_id = 0` and no payload:

- `V3TunnelCommunicator.cs:275-295`: for `TunnelPacketType.Register` the packet is
  `HeaderSize + 0 + 0` bytes and the function returns immediately after writing the two IDs.
- `V3TunnelCommunicator.cs:330`: `CreatePacket(localId, 0u, TunnelPacketType.Register)`.

`receiver_id = 0` is deliberate: 0 is never a valid client ID, so the lookup at
`TunnelV3.cs:216` (legacy) / `:97` (modern) finds nothing and the packet is not relayed
anywhere. It exists purely to create or refresh the mapping.

**Recommended client sequence, from the source:**

1. Choose a random 32-bit `local_id` (see 1.3 for the excluded values).
2. Send an 8-byte register packet `{local_id, 0}` to the tunnel's UDP port.
3. Repeat every 15 seconds forever, including during the match (see 1.6).
4. Once every peer has done the same, send game data as `{local_id, peer_id, payload...}`.

There is no acknowledgement of registration. The server sends nothing back. The only way to
know registration worked is that peer traffic starts flowing, or that a 50-byte ping is
answered (which proves reachability but not registration).

### 1.5 Relaying

After the sender's mapping is created or refreshed, the server looks up `receiver_id` and
forwards the whole datagram to that client's registered address:

LEGACY-SERVER `TunnelV3.cs:215-217`:
```csharp
TunnelClient receiver;
if (Mappings.TryGetValue(receiverId, out receiver) && !receiver.RemoteEP.Equals(sender.RemoteEP))
    Client.Client.SendTo(buffer, 0, size, SocketFlags.None, receiver.RemoteEP);
```

MODERN-SERVER `TunnelV3.cs:97-98` is identical in effect.

Consequences that matter for a host-relay star:

- **The receiver must already be registered.** If a peer has not yet sent its first packet,
  everything addressed to it is dropped silently. Register early and keep registering.
- **There is no broadcast and no multicast.** A host sending one order to 7 peers sends 7
  datagrams. That is 7x the host's uplink, which for 22-byte orders is negligible.
- **Two clients on the same source IP *and* port cannot exchange traffic**
  (`!receiver.RemoteEP.Equals(sender.RemoteEP)`). Different ports on the same IP are fine, so
  two instances on one test machine work.
- **The server does not verify that the sender owns `sender_id` beyond the source-address
  binding, and does not verify that the sender is allowed to talk to `receiver_id`.** Anyone
  who knows a peer's ID can inject packets at it. Use random IDs, and if it matters,
  authenticate at the payload level.

### 1.6 Keepalive and timeout

**Server side.**

| | LEGACY-SERVER | MODERN-SERVER |
|---|---|---|
| Client timeout | **30 s**, hard-coded | **60 s** default, `--client-timeout` / `-c`, minimum 30 |
| Sweep interval | 60 s (tied to the master-announce timer) | 60 s default, `-ai` |

- LEGACY-SERVER `TunnelClient.cs:16-22`: `public TunnelClient(int timeout = 30)`,
  `TimedOut => (now - LastReceiveTick).TotalSeconds >= Timeout`. Only ever constructed with
  the default (`TunnelV3.cs:208`).
- MODERN-SERVER `TunnelClient.cs` same property; timeout comes from
  `ServiceOptions.ClientTimeout`, defaulted at `RootCommandBuilder.cs:44` to 60 and validated
  at `:82-88` to a minimum of 30.
- Sweep: LEGACY-SERVER `TunnelV3.cs:39,80-105` (`HeartbeatTimer` at `MASTER_ANNOUNCE_INTERVAL
  = 60 * 1000`, `TunnelV3.cs:18`, which both announces to the master and removes timed-out
  mappings). MODERN-SERVER `Tunnel.cs:304-335` and `342-368`.

`TimedOut` is a computed property, so a client counts as timed out immediately at
`timeout` seconds; the *removal* from the mapping table only happens at the next 60 s sweep.
That distinction matters for reconnection, see 1.8.

**Client side.** The CLIENT re-sends its registration packet to every relay it is using every
**15 seconds**, in the lobby and in game alike:

- `V3KeepAliveMonitor.cs:32` -> `ClientConfiguration.cs:693`:
  `KeepAliveIntervalSeconds`, default **15.0**.
- `V3KeepAliveMonitor.cs:164-176`: when 15 s have elapsed,
  `_communicator.SendRegistrationToTunnels(_localId, relayTunnels, quiet: true)`.
- The comment at `V3KeepAliveMonitor.cs:159-163` says why it must run in game too: peers
  keep pinging you through the relay, and an expired registration drops their pings.

The CLIENT separately runs a peer-to-peer keepalive round trip (`KeepAlivePing` 0x0B /
`KeepAlivePong` 0x0C, 8-byte stopwatch timestamp payload, 3 misses declares a peer dead,
`V3KeepAliveMonitor.cs:33-43,239-249`). That is a client convention for liveness detection.
The **server** does not care about it and does not need it: any packet at all, including a
relayed game packet, refreshes the sender's mapping.

**What our client must do:**

> Send an 8-byte register packet `{local_id, 0}` to the tunnel every 15 seconds, from the
> same socket used for game traffic, for as long as the match lasts.

15 s is well inside the 30 s worst-case server timeout and also inside typical NAT UDP
mapping lifetimes. Match traffic alone would keep the mapping alive during play, but the
lobby and any pause would not, and a lapsed registration is expensive to recover from (1.8).

### 1.7 Built-in UDP ping (useful, and free)

`sender_id = 0`, `receiver_id = 0`, datagram length **exactly 50 bytes**. The server replies
with the **first 12 bytes of your request**, verbatim, to your source address.

- LEGACY-SERVER `TunnelV3.cs:179-187`:
  `if (size == 50 && !PingLimitReached(...)) Client.Client.SendTo(buffer, 0, 12, ...)`
- MODERN-SERVER `Tunnel.cs:12-13` (`PingRequestPacketSize = 50`,
  `PingResponsePacketSize = 12`) and `Tunnel.cs:172-219`.

The 50 bytes are otherwise uninterpreted, so put a nonce and a send timestamp in the first
12 and you get an RTT measurement with no state. Rate limits: 20 pings per IP and 5000
(legacy) / 1024 (modern) distinct IPs per 60 s window, counters cleared on every sweep
(LEGACY-SERVER `TunnelV3.cs:20-21,104,245-259`; MODERN-SERVER `RootCommandBuilder.cs:38-40`,
`Tunnel.cs:289-302,365`).

Worth using: it works from the same socket, proves UDP reachability end to end, and does not
depend on ICMP. Note the CLIENT does **not** use it. It measures tunnels with ICMP echo
instead (`CnCNetTunnel.cs:305-322`), which is why the MODERN-SERVER README lists ICMP among
the ports to open "for clients not using the built-in ping mechanism".

### 1.8 What happens when a client disappears

1. The client stops sending. Nothing happens immediately. The mapping stays and the server
   keeps forwarding packets addressed to it into the void.
2. After `ClientTimeout` seconds (30 legacy / 60 modern default) the mapping's `TimedOut`
   becomes true.
3. At the next 60 s sweep the mapping is deleted and the per-IP connection counter is
   decremented (LEGACY-SERVER `TunnelV3.cs:86-101`; MODERN-SERVER `Tunnel.cs:342-368` plus
   `TunnelV3.cs:42-48`).

**Reconnection with the same ID from a different address** is permitted, but only once the
old mapping has timed out:

LEGACY-SERVER `TunnelV3.cs:190-202`:
```csharp
if (Mappings.TryGetValue(senderId, out sender))
{
    if (!remoteEP.Equals(sender.RemoteEP))
    {
        if (sender.TimedOut && !MaintenanceModeEnabled &&
            NewConnectionAllowed(remoteEP.Address, sender.RemoteEP.Address))
            sender.RemoteEP = new IPEndPoint(remoteEP.Address, remoteEP.Port);
        else
            return;
    }
    sender.SetLastReceiveTick();
}
```
MODERN-SERVER `TunnelV3.cs:153-188` (`HandleExistingClient`) is the same.

This is the single nastiest failure mode for us, so state it clearly:

> **If your NAT rebinds your source port mid-match, the server refuses your packets until
> your own mapping times out.** That is 30 to 60 seconds of total silence, in one direction,
> that you cannot shorten by retrying, because every retry arrives from the wrong address and
> is dropped without a reply. It does not extend the timeout (the drop happens before
> `SetLastReceiveTick`), so it does clear on its own.

The 15 s keepalive exists largely to stop the NAT rebinding in the first place. A second
mitigation, if we want one, is to fall back to a fresh random ID after N seconds of silence
and re-announce it out of band, but that requires a side channel and is not something the
CnCNet client does.

Note also that the modern server, unlike the legacy one, still relays a *denied* new client's
packet onward to a registered receiver: `HandleNewClient` returns the `TunnelClient` object
whether or not `Mappings.TryAdd` succeeded (MODERN-SERVER `TunnelV3.cs:129-151`), and
`HandlePacketAsync` then falls through to the forward. The reply cannot come back, so it
presents as one-way traffic rather than as nothing. Do not rely on either behaviour.

### 1.9 Packet size limits

**Use 1024 bytes total as your hard ceiling. Payload therefore 1016 bytes.**

| | LEGACY-SERVER | MODERN-SERVER |
|---|---|---|
| Minimum accepted | 8 bytes (`TunnelV3.cs:151`) | 8 bytes (`TunnelV3.cs:28`, `Tunnel.cs:226`) |
| Maximum accepted | **1024 bytes**, `byte[] buffer = new byte[1024]` at `TunnelV3.cs:145` | `--max-packet-size` / `-mps`, default **2048**, minimum 512 (`RootCommandBuilder.cs:36,75-81`; `Tunnel.cs:53,58,226`) |

You cannot tell which implementation a given public tunnel runs. The master list carries the
*protocol* version (2/3/4), not the implementation. So assume the smaller number.

There is a further reason to respect 1024 on legacy servers. `TunnelV3.SyncReceive`
(`TunnelV3.cs:139-154`) calls `Client.Client.ReceiveFrom` with a 1024-byte buffer inside a
bare `while (true)` with **no try/catch**. On Windows an oversized datagram makes
`ReceiveFrom` throw `WSAEMSGSIZE`, which faults that `Task`, which `Program.cs:60`
(`tunnelV3Task.Wait()`) then rethrows. Reading the code, an oversized packet takes the whole
legacy V3 tunnel down. I have **not** run this against a live server, so treat the
consequence as read-from-source rather than observed, but the absence of the try/catch is
plain in the file.

The CLIENT sizes its own receive buffer at 65507 (`V3TunnelCommunicator.cs:91`,
`V3GameTunnelBridge.cs:24`), which is just "largest possible UDP payload" and says nothing
about what a server will accept.

At 22-byte orders plus an 8-byte header we are nowhere near any of this. If we ever coalesce
orders, cap the datagram at 1024.

### 1.10 How IDs are agreed between clients

The server assigns nothing in V3. The clients must agree out of band. Two schemes exist in
CnCNet, and we are free to use a third.

**Scheme A, deterministic hash (what the current client does).**
CLIENT `V3TunnelNegotiationManager.cs:70-75`:
```csharp
public uint GeneratePlayerID(string playerName)
{
    using var sha1 = SHA1.Create();
    byte[] hash = sha1.ComputeHash(Encoding.UTF8.GetBytes($"{playerName}:{host.ChannelName}"));
    return BinaryPrimitives.ReadUInt32LittleEndian(hash);
}
```
That is: `id = first 4 bytes of SHA1(UTF8(playerName + ":" + ircChannelName))` read
little-endian. Every client computes every other client's ID locally from information the
lobby already carries. Nothing is exchanged.

**Scheme B, host-assigned over the lobby channel.** The host builds a `STARTV3` message of
`id;name;ip:port;` triples in player order, IDs as **decimal strings**, and each client
parses its own and everyone else's out of it. CLIENT
`V3TunnelNegotiationManager.cs:864-902` (`GenerateV3StartPayload`) and `:820-857`
(`ApplyV3StartEntry`). In V3 static mode the `ip:port` names the shared tunnel; in dynamic
mode it is `0.0.0.0:0` and ignored.

**For us.** We already have a lobby to carry the match setup. The simplest correct thing:
the host draws N random 32-bit IDs (excluding 0 and 0xFFFFFFFF, and each other), and sends
the id-to-player table to every peer as part of the existing match-start message. That is
scheme B without the string format.

Collision risk is worth one sentence. IDs are global per server, not per match, so an
unrelated game on the same tunnel could collide. At 200 clients on a server the birthday
probability is about 5e-6 per match. The failure mode when it happens is clean and one-sided:
the second registrant's packets are dropped (source-address mismatch, section 1.8) and its
peers see nothing from it, while the first is unaffected.

### 1.11 The `CNCNET` magic layer (client convention, not protocol)

You will see this in the client source and in packet captures, so here it is, but **the
server does not parse it and we do not need it.**

CLIENT `V3TunnelCommunicator.cs:275-309`. Two shapes of packet share the 8-byte header:

```
GAME DATA and REGISTER:
  0..7    header
  8..     raw payload (empty for register)

NEGOTIATION / CONTROL:
  0..7    header
  8..13   'C' 'N' 'C' 'N' 'E' 'T'      (MAGIC_BYTES, V3TunnelCommunicator.cs:88)
  14      packet type, 1 byte
  15..    payload
```

The parser (`V3TunnelCommunicator.cs:622-656`) treats a packet as control if and only if it
is at least 15 bytes and bytes 8..13 are exactly `CNCNET`; otherwise everything past byte 8
is game data. Types, from `V3TunnelCommunicator.cs:32-67`:

```
0x01 Connected     0x06 NegotiationFailed  0x0B KeepAlivePing   0x0F TunnelList
0x02 PingRequest   0x07 Register           0x0C KeepAlivePong   0x10 TunnelSet
0x03 PingResponse  0x08 GameData           0x0D ProbeRequest
0x04 TunnelChoice  0x09 P2PInfo            0x0E ProbeReport
0x05 TunnelAck     0x0A P2PDecline
```

Note the trap: type `0x07 Register` and `0x08 GameData` are enum values that are **never
written to the wire**, because `CreatePacket` special-cases both to emit no magic and no type
byte. So a register packet is 8 bytes, not 15.

This whole layer dates from 2026-08-25 (`cd55155`). Games that used V3 before it, via the
CnCNet game spawner, put raw game bytes straight after the 8-byte header. **Our C client
should do the same: header, then our 22-byte order. No magic.** If we ever want to
interoperate with a CnCNet client we would adopt the magic layer then, not now.

One detail worth stealing regardless: the CLIENT receives the game's datagram directly into
its send buffer at offset 8 and then writes the two IDs in front of it
(`V3GameTunnelBridge.cs:212-241`), so relaying costs no copy. That works the same in C with
`recvfrom(fd, buf + 8, ...)`.

---

## 2. TUNNEL V2

### 2.1 Status: still listed, still running, but do not build against it

- It is still in the master list. The last line of the live list today is
  `221.179.161.2:50000;China;CN;...;2;0`, version 2.
- The CLIENT still supports it: `TunnelHandler.cs:42`,
  `SUPPORTED_TUNNEL_VERSIONS = [2, 3, 4]`, and `TunnelMode.V2Legacy` exists
  (`TunnelMode.cs:6-22`).
- But in MODERN-SERVER, V2 is **behind a compile-time flag**. `cncnet-server.csproj` defines
  configurations `Debug;Release;V2AndV3Debug;V2AndV3Release`, sets `EnableLegacyVersion` only
  for the `V2AndV3` ones, and for the others does
  `<Compile Remove="CnCNet\Net\Tunnel\TunnelV2.cs" />`. A plain `Release` build of the current
  server has no V2 at all. The V2 build additionally requires the ASP.NET Core runtime.

For a new client in 2026, V2 is a worse protocol (16-bit IDs, a mandatory HTTP round trip,
no implicit registration) with a shrinking server population. Recommendation: **V3 only.**

### 2.2 V2 session allocation, over HTTP

The V2 tunnel serves HTTP on the **same port number** as its UDP socket, default **50000**
(LEGACY-SERVER `TunnelV2.cs:43,56`; MODERN-SERVER `RootCommandBuilder.cs:49`).

```
GET http://<tunnel>:<port>/request?clients=<N>
```

- `N` must be 2..8 inclusive, else `400 Bad Request`
  (LEGACY-SERVER `TunnelV2.cs:190-195`; MODERN-SERVER `TunnelV2.cs:14-15,221-222`).
- Maintenance mode -> `503`. Not enough free slots -> `503`.
- Per-IP rate limit -> `429` (LEGACY-SERVER `TunnelV2.cs:143-148`, limit
  `--iplimitv2` default **4**; note MODERN-SERVER applies its limiter to `/maintenance` and
  `/status` but **not** to `/request`, `TunnelV2.cs:216-267`).
- Success body, `text/plain`:
  ```
  [12345,-6789,301]
  ```
  Literally `[` + comma-joined decimal IDs + `]`
  (LEGACY-SERVER `TunnelV2.cs:231`; MODERN-SERVER `TunnelV2.cs:254`).

The CLIENT parses it by stripping the brackets and splitting on commas
(`CnCNetTunnel.cs:267-299`), and notes at `:273` "Do not use https here as not supported by
tunnels" (MODERN-SERVER can be started with `--tunnel-v2-https`, but it is off by default).

The host performs this request, gets N IDs, and distributes one to each player over the
lobby. Unlike V3, the IDs are **created server-side by `/request`**, and a UDP packet from an
unknown `sender_id` is dropped, not registered
(LEGACY-SERVER `TunnelV2.cs:287-297`; MODERN-SERVER `TunnelV2.cs:134-142`). The HTTP step is
mandatory.

**The two servers disagree on the ID range.** LEGACY-SERVER `TunnelV2.cs:207`:
`(short)rand.Next(short.MinValue, short.MaxValue)`, so **negative IDs are possible**.
MODERN-SERVER `TunnelV2.cs:240`: `RandomNumberGenerator.GetInt32(0, short.MaxValue)`, so
**0 .. 32766 only**. A V2 client must therefore treat the ID as a signed 16-bit value and
not assume it is positive.

### 2.3 V2 UDP header

```
offset  size  field        encoding
------  ----  -----------  ---------------------------
0       2     sender_id    int16, BIG-ENDIAN (network)
2       2     receiver_id  int16, BIG-ENDIAN (network)
4       N     payload
```

- LEGACY-SERVER `TunnelV2.cs:265-266`:
  ```csharp
  short senderId = IPAddress.NetworkToHostOrder(BitConverter.ToInt16(buffer, 0));
  short receiverId = IPAddress.NetworkToHostOrder(BitConverter.ToInt16(buffer, 2));
  ```
- MODERN-SERVER `TunnelV2.cs:59-65`, same with a `(uint)` widening cast.

Minimum datagram 4 bytes (`TunnelV2.cs:258` legacy, `:21` modern). Buffer 1024 legacy /
`--max-packet-size` modern, same as V3.

**V2 and V3 headers are not compatible** and use different ports. 4-byte big-endian int16
pair versus 8-byte little-endian uint32 pair.

Other V2 differences from V3: the source address is latched on the first packet and can
**never** change (`TunnelV2.cs:290-297` legacy: if `RemoteEP == null` set it, else if it
differs, drop, with no timed-out escape hatch). So V2 cannot recover from a NAT rebind at
all within one session. V2 also has no ID-reuse path and no `/request`-free registration.

The V2 ping (`sender_id == 0 && receiver_id == 0`, 50 bytes, 12-byte echo) is identical to
V3's; it lives in the shared base class in MODERN-SERVER (`Tunnel.cs:172-219`) and is
duplicated in LEGACY-SERVER (`TunnelV2.cs:277-285`).

### 2.4 V2 `/status` and `/maintenance`

```
GET http://<tunnel>:<port>/status
```
returns `text/plain`, exactly:
```
<free> slots free.
<used> slots in use.
```
(with a trailing newline). LEGACY-SERVER `TunnelV2.cs:162-178`; MODERN-SERVER
`TunnelV2.cs:204-214`. Rate limited per IP, `429` when exceeded.

```
GET http://<tunnel>:<port>/maintenance/<password>
```
sets maintenance mode; `401` on a wrong or unset password. Operator-only.

**These endpoints exist on V2 tunnels only.** A V3-only server has no HTTP at all, so there
is no way to query a V3 tunnel's occupancy directly. Occupancy for V3 comes from the master
list (fields 5 and 6), refreshed by the server every 60 s.

---

## 3. Finding tunnel servers

### 3.1 The master list

```
GET https://cncnet.org/master-list
```

CLIENT `ClientConfiguration.cs:500`:
```csharp
public string CnCNetTunnelListURL => networkDefinitionsIni.GetStringValue(SETTINGS,
    "CnCNetTunnelListURL", "https://cncnet.org/master-list");
```
Fetched with a 10 s timeout, 2 attempts, then falls back to a `tunnel_cache` file on disk
(`TunnelHandler.cs:363-419`).

Plain text, one record per line, `;`-separated, **with a header line that must be skipped**
(`TunnelHandler.cs:441`, `serverList.Skip(1)`). Live data, fetched during this research:

```
address;country;countrycode;name;password;clients;maxclients;official;latitude;longitude;version;distance
51.75.126.186:50001;France;FR;[EU]Hottwire.me;0;9;100;2;0;0;3;0
149.28.164.124:50001;Australia;AU;CnCNet Australia;0;4;250;2;0;0;3;0
49.13.152.109:50001;Germany;DE;CnCNet Europe;0;7;250;2;0;0;3;0
45.76.102.31:50001;Japan;JP;CnCNet Japan;0;4;250;2;0;0;3;0
149.28.195.145:50001;United States;US;CnCNet Silicon Valley;0;4;250;2;0;0;3;0
```

162 lines total at the time of fetching. Field meanings, from `CnCNetTunnel.Parse`
(`CnCNetTunnel.cs:46-90`):

| # | Field | Type | Notes |
|---|---|---|---|
| 0 | `address:port` | split on `:` | Address is a literal IP in practice; the client does `IPAddress.Parse` on it (`V3TunnelCommunicator.cs:178,501`), so a hostname here would throw. |
| 1 | country | string | |
| 2 | countrycode | string | |
| 3 | name | string | Server operators cannot use `;` in it (validated server-side). |
| 4 | password | `!= "0"` means password-required | The client skips these entirely (`TunnelHandler.cs:450-451`). |
| 5 | clients | int | Occupancy at the server's last 60 s announce. |
| 6 | maxclients | int | |
| 7 | status | int | `2` = official, `1` = recommended, else neither. |
| 8 | latitude | double, invariant culture | 0 in the live data. |
| 9 | longitude | double, invariant culture | 0 in the live data. |
| 10 | **version** | int | `2` = V2 protocol, `3` = V3 protocol, `4` = matchmaking server. |
| 11 | distance | double | 0 in the live data. |

**Version 4 is not a fourth protocol.** `CnCNetTunnel.cs:216-229`: it is a *role* label for
matchmaking servers, which speak plain V3 on the wire. `V3TunnelCommunicator.cs:196-197`
accepts 3 and 4 for routing; `V3TunnelNegotiationManager.cs:82-88` accepts only 3 for
carrying a game. **For us: use version 3 only.**

Client-side filtering, `TunnelHandler.cs:441-460`: skip the header, skip password-protected
(`:450`), keep only versions in `[2,3,4]` (`:453`), deduplicate on `address:port` (`:456`).

### 3.2 Practical warning: the master list is behind Cloudflare bot protection

This is a real finding, not a note. During this research a plain `curl` request to
`https://cncnet.org/master-list` returned:

```
HTTP/1.1 429 Too Many Requests
Cf-Mitigated: challenge
Content-Type: text/html; charset=UTF-8
Server: cloudflare
```

with a JavaScript interstitial body, on the very first request, and again with a Chrome
User-Agent. A browser-based fetch of the same URL succeeded and returned the real list. So
the block is on client fingerprint, not on request rate, and setting a User-Agent header is
not sufficient.

**Implication for a C client using libcurl:** expect this to fail, and expect it to fail with
a `200`-looking HTML body in some configurations rather than an obvious error. Design for it:

- Ship a bundled default tunnel list and treat the download as an optimisation, which is
  exactly what the CLIENT does with its `tunnel_cache` file (`TunnelHandler.cs:364,371-375`).
- Validate the response before trusting it: first line must start with `address;country;`.
- If we want a reliable list, mirror it ourselves rather than fetching cncnet.org at runtime.

I have **not** determined which TLS or header fingerprint Cloudflare accepts here, and I am
not going to guess at one.

### 3.3 The other `/status`, and what it is not

`https://api.cncnet.org/status` (CLIENT `ClientConfiguration.cs:502`,
`CnCNetPlayerCountURL`) returns per-game player counts, not tunnels:

```json
{"cncnet5":848,"cncnet5_td":2,"cncnet5_ra":96,"cncnet5_ts":23,"cncnet5_dta":0,
 "cncnet5_d2":2,"cncnet5_yr":349,"cncnet5_mo":308,"cncnet5_rr":0,"cncnet5_cncr":2,
 "cncnet5_re":0,"cncnet5_pp":0,"total":848}
```
(fetched live; `cncnet5_td` is Tiberian Dawn). Useful for a player-count display, useless for
tunnel discovery. Do not confuse it with a V2 tunnel's own `/status` (section 2.4).

### 3.4 The master-announce endpoint, for completeness

Servers announce themselves to the master with a GET:

```
<master-url>?version=<2|3>&name=<urlenc>&port=<n>&clients=<n>&maxclients=<n>
            &masterpw=<urlenc>&maintenance=<0|1>[&address2=<ipv6>]
```

LEGACY-SERVER `TunnelV3.cs:112-131` (default master
`http://cncnet.org/master-announce`, `Options.cs:31`); MODERN-SERVER `Tunnel.cs:250-258`
(default `https://cncnet.org/api/v1/master-announce`, `RootCommandBuilder.cs:20`; adds
`address2` for IPv6, expects the body `OK`). **A client never calls this.** Listed only so it
is not mistaken for a client endpoint.

---

## 4. Limits, in one place

| Limit | LEGACY-SERVER | MODERN-SERVER | Where |
|---|---|---|---|
| V3 UDP port | 50001 | 50001 | `Options.cs:10` / `RootCommandBuilder.cs:8` |
| V2 UDP+TCP port | 50000 | 50000 | `Options.cs:13` / `RootCommandBuilder.cs:49` |
| STUN ports (UDP) | 8054, 3478 | 8054, 3478 | `Program.cs:28-32` / `CnCNetBackgroundService.cs:17-18` |
| Max clients per **server** | 200 default, min 2 | 200 default, min 2 | `Options.cs:19` / `RootCommandBuilder.cs:12,61-67` |
| Max clients per **session** | n/a, V3 has no sessions | n/a | see below |
| Clients per IP (V3) | 8 default, counted **per IP** | 8 default, counted **per IP:port** | `TunnelV3.cs:221-243` / `TunnelV3.cs:190-212` |
| Clients per IP (V2 HTTP) | 4 default | 8 default (shares `--ip-limit`) | `Options.cs:37` / `RootCommandBuilder.cs:22` |
| Global V2 HTTP requests per 60 s | 1000 | 1000 | `TunnelV2.cs:18,316` / `TunnelV2.cs:13,271` |
| Client timeout | 30 s fixed | 60 s default, min 30 | `TunnelClient.cs:16` / `RootCommandBuilder.cs:44,82-88` |
| Mapping sweep / announce | 60 s | 60 s default | `TunnelV3.cs:18` / `RootCommandBuilder.cs:42` |
| Min datagram (V3) | 8 | 8 | `TunnelV3.cs:151` / `TunnelV3.cs:28` |
| Max datagram | 1024 | 2048 default, min 512 | `TunnelV3.cs:145` / `RootCommandBuilder.cs:36,75-81` |
| Pings per IP per 60 s | 20 | 20 | `TunnelV3.cs:20` / `RootCommandBuilder.cs:40` |
| Distinct ping IPs per 60 s | 5000 | 1024 | `TunnelV3.cs:21` / `RootCommandBuilder.cs:38` |
| `/request` clients | 2..8 | 2..8 | `TunnelV2.cs:190` / `TunnelV2.cs:14-15` |
| Maintenance command rate | 1 per 60 s | 1 per 60 s | `TunnelV3.cs:19` / `TunnelV3.cs:14` |
| IPv6 | no, binds IPv4 | yes, binds `IPv6Any` dual-stack | `TunnelV3.cs:141` / `Tunnel.cs:46` |

Notes on three of these:

**"Max clients per session" does not exist in V3.** The server has no concept of a session,
a lobby or a game. It has a flat `sender_id -> address` table shared by every game on that
server, capped at `--max-clients` (200). Our 8-peer match consumes 8 of those 200 slots.
Only V2 groups IDs, and only at allocation time via `/request?clients=N`, `N` in 2..8. In
practice CnCNet games cap at 8 players; that comes from the games, not the tunnel.

**Session lifetime does not exist either.** There is no expiry other than the per-client
inactivity timeout. Keep sending and your ID lives forever.

**The modern per-IP limit is weaker than it looks.** `IsNewConnectionAllowed`
(MODERN-SERVER `TunnelV3.cs:190-212`) keys the counter on `SocketAddress.GetHashCode()`,
which includes the source **port**, whereas the legacy server keyed on
`IPAddress.GetHashCode()` (`TunnelV3.cs:223`). So `--ip-limit 8` on a modern server is
really "8 per IP:port", which almost never binds. This is either a deliberate change or a
regression; the source does not say which, so I am reporting it as an observed difference.
It does not affect us either way at 8 peers.

---

## 5. The STUN service on 8054 and 3478

Not part of the tunnel, but it is the same daemon and it is the cheap way to learn your own
public IP:port from the same socket you will send game traffic on. Include it or not.

**Request:** 48 bytes exactly. Bytes 0-1 = `26262` (0x66B6) big-endian. Bytes 2-47 random.

**Response:** 40 bytes. Bytes 0-5 are XORed with `0x20`. After un-XORing:
- bytes 0-3: your external IPv4
- bytes 4-5: your external port, big-endian
- bytes 6-7: `26262` big-endian, **not** XORed, use it to validate
- bytes 8-39: random padding

Sources: LEGACY-SERVER `PeerToPeerUtil.cs:17,25-44,65-113`; MODERN-SERVER
`PeerToPeerUtil.cs:10-13,108-158`; CLIENT `StunHelper.cs:14-22` documents exactly this layout
and `:25-59` implements it.

Rate limit: 20 requests per IP per 60 s, 5000 distinct IPs per 60 s
(`PeerToPeerUtil.cs:14-16` legacy, `:10-12` modern).

Two caveats:

- **The 48-byte length check is exact.** Both servers ignore anything that is not 48 bytes
  (`PeerToPeerUtil.cs:77` legacy, `:89` modern).
- **IPv6 responses have a different layout, and the client cannot parse them.** MODERN-SERVER
  `PeerToPeerUtil.cs:127-142` writes a 16-byte address for a non-IPv4-mapped peer, putting the
  port at 16-17 and the marker at 18-19, and XORs the first 18 bytes. `StunHelper.ParseResponse`
  only ever reads the 4-byte form, so it would misparse an IPv6 response. Query over IPv4.
- LEGACY-SERVER randomises its 40-byte send buffer **once, at construction**
  (`PeerToPeerUtil.cs:33`), so bytes 8-39 are identical in every response for the process
  lifetime. MODERN-SERVER randomises per response (`:142`). Do not read anything into the
  padding.

The CLIENT queries several STUN servers and only trusts the result if at least 2 agree,
treating disagreement as symmetric NAT (`StunHelper.cs:61-134`). Worth copying if we ever
attempt direct peer-to-peer; not needed for pure relay.

---

## 6. Minimal client, as pseudocode

Everything a relay-only V3 client needs.

```c
/* --- setup ------------------------------------------------------------ */
fd = udp_socket();                      /* one socket, ephemeral local port  */
bind(fd, 0.0.0.0:0);
tunnel = { ip, 50001 };                 /* from master list, version == 3    */

local_id = random_u32_excluding(0, 0xFFFFFFFF);   /* and every peer's id     */
/* host draws all N ids and sends the id table over the existing lobby link */

/* --- register, and keep registering ----------------------------------- */
void send_register(void) {
    uint8_t p[8];
    put_u32_le(p + 0, local_id);
    put_u32_le(p + 4, 0);               /* receiver 0 = register only        */
    sendto(fd, p, 8, tunnel);
}
send_register();
/* then every 15 s, forever, in lobby and in game */

/* --- send an order to one peer ---------------------------------------- */
void send_to_peer(uint32_t peer_id, const void *data, size_t n) {
    /* n + 8 must stay <= 1024 */
    uint8_t p[1024];
    put_u32_le(p + 0, local_id);
    put_u32_le(p + 4, peer_id);
    memcpy(p + 8, data, n);
    sendto(fd, p, 8 + n, tunnel);
}
/* host relay star: loop this over all peers, one datagram each */

/* --- receive ---------------------------------------------------------- */
n = recvfrom(fd, buf, sizeof buf, &from);
if (!same_endpoint(from, tunnel)) continue;   /* ignore anything else       */
if (n < 8) continue;
sender   = get_u32_le(buf + 0);
receiver = get_u32_le(buf + 4);
if (receiver != local_id) continue;           /* not for us; shouldn't happen */
handle_order(sender, buf + 8, n - 8);

/* --- optional reachability probe -------------------------------------- */
/* 50 bytes exactly, sender=0, receiver=0; server echoes bytes 0..11 back  */
uint8_t ping[50] = {0};
put_u32_le(ping + 0, 0);
put_u32_le(ping + 4, 0);
put_u32_le(ping + 8, my_nonce);         /* comes back in the 12-byte reply   */
random_fill(ping + 12, 38);
sendto(fd, ping, 50, tunnel);
```

Design points that follow from the source:

- **One socket for everything.** Registration, game data and pings must share a socket, or
  the NAT mapping the server has recorded is not the one your game traffic comes from.
- **Every peer registers independently.** The host does not register anyone. A peer that has
  not registered cannot be sent to.
- **Filter by source endpoint.** Everything legitimate arrives from the tunnel's IP:port.
- **Check `sender` against the expected peer set** before acting on a packet. The server does
  not, and any client that knows your ID can reach you.
- **Budget the keepalive at 15 s** and treat 30 s as the deadline you must never miss.

---

## 7. Open items and things I could not verify

1. **Which implementation any given public tunnel runs.** Not discoverable from the master
   list, which carries the protocol version only. Consequence: assume the stricter of the two
   for every limit (1024-byte datagrams, 30 s timeout).
2. **The Cloudflare fingerprint that `cncnet.org/master-list` accepts.** Observed: plain curl
   and curl with a Chrome User-Agent both get `429` + `cf-mitigated: challenge`; a browser
   fetch succeeds. I did not determine what makes a request acceptable, and did not guess.
   Mirror the list or bundle it.
3. **Whether an oversized datagram actually kills a legacy V3 tunnel.** Read from the source
   (`TunnelV3.cs:139-154` has no try/catch around `ReceiveFrom`; `Program.cs:60` waits on
   that task) but not tested against a live server. UNVERIFIED as behaviour, certain as code
   structure. Either way, stay under 1024.
4. **Whether the legacy 2020 server is deployed anywhere today.** The CnCNet-org repo's last
   code commit is 2020-08-23 and the .NET rewrite is what the CnCNet Docker image packages,
   which suggests the modern one dominates, but I found no inventory of what each of the 162
   listed servers runs. UNVERIFIED.
5. **Whether any tunnel rate-limits *relayed* traffic.** I found rate limits on pings, on V2
   HTTP requests and on new connections per IP, and none on the relay path itself. Absence of
   evidence: I read both servers' relay paths and there is no throttle in them, but I cannot
   rule out something in front of the process on a given host.
6. **Server-side packet reordering or duplication guarantees.** None are made and none are
   implemented; the relay is a straight `sendto`. Plain UDP semantics apply end to end. Our
   order layer must already tolerate loss and reordering.
