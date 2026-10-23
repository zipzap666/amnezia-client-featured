# Windows running applications regression checks

These standalone tests do not instantiate the VPN client, access user settings, or contact its service.
They launch and terminate only their own temporary test processes.

From the repository root (Qt 6.10.1 / Visual Studio 2022):

```powershell
.build-tools/venv/Scripts/cmake.exe -S tests/runningApps -B deploy/build/runningAppsTests -DCMAKE_PREFIX_PATH=C:/Qt/6.10.1/msvc2022_64
.build-tools/venv/Scripts/cmake.exe --build deploy/build/runningAppsTests --config Release
$env:PATH = 'C:\Qt\6.10.1\msvc2022_64\bin;' + $env:PATH
.build-tools/venv/Scripts/ctest.exe --test-dir deploy/build/runningAppsTests -C Release --output-on-failure
```

Coverage: case/separator path normalization, real-process deduplication, full-path records,
refresh after process start/exit, exit during enumeration, HANDLE lifetime, and the production
QML JavaScript for literal name/path search, multiple selection, deselection, and refresh reset.

Manual UI checks (not automated): with VPN disconnected, open application split tunneling,
press +, choose the running-applications source, search by name and full path, select several
entries and add them. Reopen to see Already added. Refresh resets selection. Verify file
selection remains available and selecting the same EXE does not duplicate the rule. Restart
the selected application to verify the saved EXE path remains in the list. VPN routing and
Android regression checks were not performed. EXE icons are extracted in the worker with a generic fallback. Tests also check valid PNG output, missing files, and icon handle lifetime.
