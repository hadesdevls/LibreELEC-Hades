# Bluetooth

## Why a device disconnected

`kodi.log` says why each Bluetooth device disconnected, in a line such as:

```
SETTINGS: device_disconnected # Bluetooth: AA:BB:CC:DD:EE:FF (input-keyboard, usb:v0955p7214d0100) disconnected: org.bluez.Reason.Authentication (Connection terminated due to authentication failure)
```

The address, the kind of device and the maker's IDs are given; the name of the device is not (it may be a person's). The filter of the diagnostics and of Submit Log turns the address into a marker, and the name of the device into one wherever another line names it ([diagnostics.md](diagnostics.md#what-the-filter-takes-out)).

| Reason | What it means | What to do |
| --- | --- | --- |
| `org.bluez.Reason.Authentication` | The device did not accept the key that this system keeps for it: it was paired again with another system since (Windows on this PC, for example), or it lost its pairing. The system tries three more times, then stops connecting to it; a notice on the screen says so, once. | Pair it again here (remove it first in *LibreELEC settings > Bluetooth*), or give both systems the same key: see the next section. |
| `org.bluez.Reason.Timeout` | The radio link was lost: nothing was heard from the device for its supervision timeout (`bluetooth.txt` of the diagnostics has it). Distance, interference, Wi-Fi on 2.4 GHz on the same card, or the controller. | Try the device closer, the Wi-Fi on 5 GHz or a network cable, and a start from a full power-off (see the Intel note below). |
| `org.bluez.Reason.Remote` | The device ended the connection itself. Many remotes do that after a while without a key press, to save their battery, and connect again at the next press. | Nothing, unless key presses are lost. |
| `org.bluez.Reason.Local` | This system ended it: the Bluetooth standby of the LibreELEC settings (with the screensaver, or after its idle time), which is logged first as `Bluetooth standby: disconnecting`, or a disconnection chosen in the settings. | Turn off the standby of that device in its menu in *LibreELEC settings > Bluetooth*. |
| `org.bluez.Reason.Unknown` | Another reason. | Send the diagnostics. |

## A device used in Windows and in LibreELEC Hades on one PC

A Bluetooth device keeps one key for each computer that it is paired with, and it knows a computer by the address of its Bluetooth adapter. Windows and LibreELEC Hades on one PC use the same adapter, so to the device they are one computer: pairing it in one system replaces the key that the other one has, and in that other system it then disconnects as soon as it connects (`org.bluez.Reason.Authentication`). There are three ways to live with it:

1. Use the device with one of the two systems only.
2. Pair it again each time you change system.
3. Give both systems the same key. Pair the device in LibreELEC Hades first, then in Windows, and then copy to LibreELEC Hades the key that Windows made:
   1. In Windows the keys are under `HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\BTHPORT\Parameters\Keys\<adapter address>\<device address>` (each address as 12 hex digits), which only the SYSTEM account can read. One way to export them with Windows' own tools, in a Command Prompt run as administrator:
      ```
      schtasks /create /tn hades-bt-keys /sc once /st 00:00 /ru SYSTEM /tr "reg export HKLM\SYSTEM\CurrentControlSet\Services\BTHPORT\Parameters\Keys C:\hades-bt-keys.reg /y"
      schtasks /run /tn hades-bt-keys
      schtasks /delete /tn hades-bt-keys /f
      ```
   2. In `C:\hades-bt-keys.reg`, under the address of the device: `LTK` (the key), `KeyLength`, `EDIV`, `ERand`, and `IRK` where the device has one.
   3. Start LibreELEC Hades, log in with SSH, run `systemctl stop bluetooth`, and edit `/storage/.cache/bluetooth/<adapter address>/<device address>/info` (the addresses with colons, in capitals). In its section `[LongTermKey]`: `Key=` the 16 bytes of `LTK` as hex digits, in the same order, in capitals and without commas; `EncSize=` the value of `KeyLength` in decimal; `EDiv=` the value of `EDIV` in decimal; `Rand=` the value of `ERand` in decimal. Leave `Authenticated=` as it is. Then run `systemctl start bluetooth`.
   4. Delete `C:\hades-bt-keys.reg`: with those keys and the address of the device, someone else could pose as that device to your PC.

   This has not been tried on a PC of the project. Sources differ on the order of the bytes of `IRK` (some say that Windows keeps it reversed); a device with a public or a static address does not need it. If the device still disconnects with `org.bluez.Reason.Authentication`, pair it again in LibreELEC Hades: it then works there, and has to be paired again in Windows.

## Intel controllers after a restart from Windows

The older Intel Bluetooth controllers, those whose line in the kernel log says `Legacy ROM` (for example the Wireless-AC 3168), get a patch of their firmware from the system each time they are powered on. After a restart from Windows they keep the patch that Windows loaded, and LibreELEC Hades does not load its own: the kernel log says `Intel device is already patched`. If a device misbehaves after a restart from Windows, turn the PC off, unplug it for ten seconds, and start LibreELEC Hades directly; the kernel log then names the firmware file that was loaded.

## USB power saving

A Bluetooth controller on USB may sleep while no device is connected to it (`btusb.enable_autosuspend`, on, as in LibreELEC). To try without it, over SSH:

```
echo "options btusb enable_autosuspend=0" > /storage/.config/modprobe.d/btusb.conf
```

and restart; `/sys/module/btusb/parameters/enable_autosuspend` then shows `N`. Delete the file to undo it.
