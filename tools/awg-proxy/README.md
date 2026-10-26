# AmneziaWG proxy helper

Windows x64, Go 1.26.0, AWG v3.1.20260814. The engine and gVisor versions
are pinned by `go.mod` and `go.sum`. The Conan recipe builds and packages the
helper beside the Qt executable, with third-party license notices.

The helper does not create a system adapter or change Windows DNS, routes,
firewall or proxy settings. The Qt `WindowsSystemProxyManager` owns the
current user's system proxy transaction and its persistent recovery journal.

## Control protocol

Private stdin/stdout, one JSON object per line, maximum input size 1 MiB.
All messages carry `version: 1` and an opaque `session` identifier. The first
command is `start` with `native`, `dns`, `httpPort`, `socksPort`, and `rules`
(`enabled`, `mode`, `sites`). The only subsequent command is `stop`.
EOF terminates the helper. Never log commands: `native` contains private keys.

Events: `ready` (listeners and handshake), `unavailable` / `available`
(tunnel DNS health), `counters`, and `error`. Counters contain stream `rx` /
`tx`, `directRx` / `directTx`, and separate AWG `peerRx` / `peerTx` values.
Upstream UAPI output contains private keys and must never be forwarded.

HTTP forwarding and HTTPS CONNECT do not terminate browser TLS. SOCKS5
supports unauthenticated TCP CONNECT on loopback only; BIND and UDP ASSOCIATE
return command-not-supported. Windows system proxy configuration uses the
HTTP listener for both HTTP and HTTPS. Manual SOCKS5 remains available.

All destination names are resolved inside AWG, including direct-route names.
Only the AWG Endpoint is bootstrapped with the system resolver. Explicit site
rules can select a direct TCP connection; AWG failure never selects it.

## Windows startup details

Use the official Windows default UDP bind. With the pinned dependencies,
`NewStdNetBind` failed loopback transmission with `WSAEFAULT` in `wsasendmsg`.
The Windows default bind passed the local handshake and forwarding tests
without an administrator token.

`startupTUN` completes the engine's first read without a packet after applying
configuration. AWG otherwise snapshots S4 before IpcSet while starting its TUN
reader, corrupting the first packet when transport padding is nonzero.

## Validation

Run `go test -buildvcs=false -timeout 90s ./...` and `go vet -buildvcs=false ./...`.
Tests use local userspace AWG peers and HTTP/HTTPS/DNS services; no VPS, system
adapter, system DNS or system proxy configuration is involved. Coverage includes
AWG 1/2/3 parameters, S4, HeaderProtectionKey, I1-I5, header ranges, HTTP,
HTTPS CONNECT, SOCKS5 domain requests, explicit direct routes, no direct
fallback, unsupported commands, occupied ports and shutdown.

Qt recovery, snapshot and QML tests live in `tests/awgProxy`. Recovery tests
replace all WinINet operations with an in-memory backend. Existing regression
suites are `splitTunnelSession`, `runningApps`, and `trafficStatistics`.

The real client must not be started during automated validation because it can
auto-connect. A separate manual test against an existing VPS remains necessary
before release. The system proxy affects only applications honoring Windows
settings; UDP, WebRTC, independent browser DNS and services can bypass it.
