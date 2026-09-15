# Device access on Windows/WSL (for iOS installs)

Getting a physical iPhone visible to a Linux build environment on Windows. This
cost real time to work out, so it is written down.

## The short version

Forward **Apple's own Windows mobile-device service** into WSL over TCP. Do **not**
use `usbipd-win` for iOS.

## Why not usbipd

`usbipd-win` corrupts iOS bulk transfers. The symptom is a build that enumerates the
phone fine (`idevice_id -l` and `xtool devices` both list it) and then hangs at
`[Connecting] 100%` with `Error: opTimeout`, while `journalctl -u usbmuxd` shows:

```
device_control_input: ERROR (on device): asyncReadComplete, message was too large (65536 bytes, max = 65535)
device_control_input: Got unhandled payload type 4
```

That is [usbipd-win#959](https://github.com/dorssel/usbipd-win/issues/959), **still
open** as of the latest release — upgrading does not fix it.

## Setup (Windows side, once, elevated)

```powershell
# Provides the local usbmux mux on port 27015
winget install --exact Apple.AppleMobileDeviceSupport
winget install --id 9NP83LWLPZ9K --source msstore    # Apple Devices app

$wslIf = Get-NetIPAddress -AddressFamily IPv4 |
         Where-Object { $_.InterfaceAlias -like '*WSL*' } | Select-Object -First 1

New-NetFirewallRule -DisplayName 'WSL-usbmux-forward' -Direction Inbound -Action Allow `
  -InterfaceAlias $wslIf.InterfaceAlias -Protocol TCP -LocalPort 27015

netsh interface portproxy add v4tov4 listenport=27015 listenaddress=$wslIf.IPAddress `
  connectport=27015 connectaddress=127.0.0.1
```

If you previously bound the phone with usbipd, `usbipd detach` + `usbipd unbind`
first so Windows owns the device.

## Each session (WSL side)

```bash
HOSTIP=$(ip route list default | awk '{print $3}')
export USBMUXD_SOCKET_ADDRESS="$HOSTIP:27015"
sudo systemctl stop usbmuxd && sudo rm -f /var/run/usbmuxd   # a local socket SHADOWS the forward
idevice_id -l          # -> your UDID
xtool devices          # -> "your iPhone [usb]: <UDID>"
```

**The local socket shadowing the forward is the trap.** If `/var/run/usbmuxd` exists,
libimobiledevice talks to the local daemon and never sees the forward, and you get an
empty device list with no error.

## Verifying the mux directly

If it "connects but lists no devices", probe the mux yourself. **The usbmux header
is 16 bytes, not 8**:

```python
import socket, plistlib, struct
req = plistlib.dumps({"MessageType": "ListDevices", "ClientVersionString": "usbmuxd-1.1.1",
                      "ProgName": "probe", "kLibUSBMuxVersion": 3})
s = socket.create_connection(("127.0.0.1", 27015), timeout=25)
s.sendall(struct.pack("<IIII", 16 + len(req), 1, 8, 1) + req)
print(plistlib.loads(s.recv(65536)[16:]))   # expect a DeviceList with your UDID
```

`devices: 0` while Windows PnP shows *Apple iPhone* (Status OK) means Apple's
service lost the device. Restart the Apple stack and force re-enumeration: kill
`AppleDevices` / `AppleMobileDeviceProcess` / `AppleMobileDeviceLauncher`, then
`pnputil /restart-device <InstanceId>` for each `*VID_05AC*`.

## Installing

```bash
./wsl.sh deploy        # build core, build+sign+install+launch
```

The phone must be **unlocked** and you must tap *Trust This Computer*. A locked
phone blocks the whole install while still enumerating: `ideviceinfo` returns
`Could not connect to lockdownd: Password protected (-17)`, and `xtool dev` stalls
at `Please unlock your device...`. Note `idevice_id -l` still lists the device
while locked — listing succeeds, lockdown does not.
