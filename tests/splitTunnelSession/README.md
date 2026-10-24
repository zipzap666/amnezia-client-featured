# Editing split tunneling while connected

This change is client-only. The daemon and driver retain their 5.0.3.0 implementation.

## Saved settings versus an active session

`SecureAppSettingsRepository::splitTunnelSnapshot()` copies both enable flags, both modes,
and every application/site list. `ConnectionController` captures this value before creating
protocol configuration and sends it with that configuration to the VPN worker. Configuration,
app rules sent over IPC, site routes, delayed routing and DNS callbacks use this attempt.
Missing domain addresses are resolved within the attempt before starting the protocol.
Existing session DNS lookups never write rules back to the settings repository. Their
callbacks are invalidated when the session stops, fails or is replaced.

`SplitTunnelSession` is owned by the shared connection controller, independent of QML pages.
It compares saved content against the last successfully applied snapshot (or first attempt
before any success). Completion commits the attempt, not the most recently edited settings.
Errors and cancellation do not commit. Lists are treated as sets. Windows EXE paths compare
case-insensitively with normalized separators; domain keys ignore case/trailing dots. Domain
IP values are DNS cache, so they are excluded from comparison. Explicit IP/CIDR keys remain
rules. All stored lists and selected modes are compared, including currently disabled lists.

## Reconnect lifecycle

The explicit button keeps the active server ID and container/protocol, waits for Disconnected,
then queues the existing connection pipeline. Settings are captured when this new attempt
starts. Repeated requests are rejected; the normal disconnect button cancels pending work.
Automatic network/wakeup recovery reuses the current protocol and snapshot; it does not
apply saved edits. It also waits for teardown before restarting.

The WireGuard client no longer treats sending `deactivate` as completion. It waits for the
existing daemon `disconnected` reply, then requests `status` and waits for `connected=false`.
This second round trip follows final daemon cleanup, which occurs after the disconnected
notification. No service IPC extension and no arbitrary reconnect timer are required.
Socket/backend errors fail the operation rather than reporting applied settings.

## Automated checks

Configure/build with Qt Core and Network:

```
.build-tools/venv/Scripts/cmake.exe -S tests/splitTunnelSession -B deploy/build/splitTunnelSessionTests -DCMAKE_PREFIX_PATH=C:/Qt/6.10.1/msvc2022_64
.build-tools/venv/Scripts/cmake.exe --build deploy/build/splitTunnelSessionTests --config Release
```

With Qt's bin directory on PATH:

```
.build-tools/venv/Scripts/ctest.exe --test-dir deploy/build/splitTunnelSessionTests -C Release --output-on-failure
```

Tests cover saved/attempt isolation, repeated edits, revert, older connection completion,
DNS cache updates, domain and Windows path normalization, import/clear semantics, repeated
reconnect clicks, disconnect ordering, cancel/failure retention, manual reconnect and recovery.
A second target compiles the actual LocalSocketController parser with a logging stub and feeds
simulated daemon replies. It checks that neither sending deactivate nor receiving disconnected
alone allows restart; stopped status is required. It also checks backend failure and cancellation
before initialization. These tests never connect to a real service or alter networking/settings.

## Manual checks still required

Use the portable EXE without installing it. For this machine's connectivity test, disable zapret
first (its previously confirmed interaction with split tunneling is independent of this change).

1. Connect using the usual server; note the server and protocol. Add/remove applications or
   sites while connected. Edit modes, toggles, import and clear lists. Connection must stay up.
2. Navigate Home -> Settings -> app/site pages: the banner must persist. Restore the exact
   original rules: it must disappear without reconnecting.
3. Make edits and click Reconnect once or repeatedly. Verify temporary interruption, same server
   and protocol, and that the banner clears only after successful connection with current rules.
4. Edit again during Connecting: successful completion of an older attempt must leave the banner.
5. Cancel using the main connection button during reconnect; no later automatic restart should
   occur. Test a failed connection and normal manual disconnect/connect.
6. Add/remove a domain while resolution is pending and switch modes: late DNS replies must not
   resurrect removed rules or change another mode. DNS cache refresh alone must not show a banner.
7. Exercise the running-app chooser and Windows exclusion-only mode. Other settings stay locked.

Full UI navigation, real VPN routing, real network/wakeup recovery and other platforms require
manual testing. Unit tests and a successful Windows Release build do not establish those results.
