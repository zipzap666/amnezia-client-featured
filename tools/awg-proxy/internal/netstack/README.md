# Proxy TCP stack

`tun.go` is copied from `github.com/amnezia-vpn/amneziawg-go/v3`,
tag `v3.1.20260814`, `tun/netstack/tun.go`, under its original MIT license.
The original file SHA-256 is
`a5c29a6d68b63012d012a2c978fbfff9e144407fb0a75215593f77781802fe82`.

Changes select gVisor's existing CUBIC congestion controller instead of Reno,
set the TCP send buffer default to 4 MiB (maximum 8 MiB), and release packet
references and views after use so gVisor can recycle their storage. The upstream
constructor does not expose its stack or transport options, so this package
is kept locally without modifying the pinned AWG engine or the Go module
cache. Keep the upstream source and these changes in sync when upgrading AWG.

Routing, DNS, encryption and packet formats are unchanged.
The original third-party license is included in the helper's license bundle.
