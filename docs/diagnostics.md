# Diagnostics and logs

What to send with a problem report about LibreELEC Hades, and how to get it. "Reporting problems" in the [README](../README.md#reporting-problems) says what else a report needs.

## The diagnostic switch

Off by default. While it is on, every start of the PC leaves a record of what the system saw in the folder `hades-diag` of the drive's boot partition: the one Windows shows as LIBREELEC when the drive is plugged into it. It changes no setting and turns nothing off, and nothing is sent anywhere: the files stay on the drive.

- **Turning it on:** tick "Save diagnostics there" in the Windows installer when you install or update. Or, with the drive plugged into a Windows PC, rename the file `DIAG-OFF.TXT` at the top of that partition to `DIAG-ON.TXT`. Where Windows hides the ending and shows `DIAG-OFF`, rename it `DIAG-ON`: any name that starts with `DIAG-ON` counts.
- **Turning it off:** rename the file back or delete it, or update with the box unticked.

The diagnostics only read what the system is doing, with three exceptions a minute after each start, meant for a TV that is plugged in and not seen: they ask the empty HDMI and DisplayPort sockets once more (not once a video has been opened); they list the PC's devices, which wakes graphics that were asleep; and, where no TV or monitor is seen on any socket, they have the kernel write more about the displays for about three minutes, or until a video is opened.

## What is saved

Each start of the PC has a folder of its own in `hades-diag`: `start-1`, `start-2` and so on. The number goes on from the start before, also after an update or once the folders have been deleted (the last one is kept in `/storage/.cache/hades-diag/last-start`), so a name is never given to two starts. The last eight are kept, so a drive taken from one PC to the next keeps what each of them had to tell. Each folder has `start.txt`, which says which start it is of (its number, the build and when the PC started), and each capture in it has `build.txt`, the build that wrote it. What is in a folder is always of that one start: a folder of the same name that is of another start is emptied before this start writes there, and `start.txt` then says what it was. A `README.txt` in `hades-diag` says what each part is.

| Folder | When | What |
| --- | --- | --- |
| `1-minute`, `4-minutes` | About 1 and 4 minutes after Kodi starts | The graphics and what is connected to each (with the EDID of every display), the kernel log, the end of `kodi.log`, the system's warnings, sound and USB devices, network state, services, the clock, with NVIDIA graphics which NVIDIA driver was chosen at start (615 or 580), and Bluetooth (`bluetooth.txt`: the controllers and their firmware, the settings, and of each paired device its kind, its state and the connection parameters that it asked for; never a key, and its name and alias only as their markers). The first also has what the empty sockets answered when they were asked again (`redetect.txt`), the PC's devices (`lspci.txt`) and the firmware's ACPI tables (`acpi-tables.tar.gz`, without the one that holds a Windows product key) |
| `during-dv-1` to `during-dv-6` | About 15 s after each of the first six Dolby Vision videos of a start begins | What is really sent to the TV: the connector's properties with the Dolby InfoFrame, the driver's atomic state. With NVIDIA graphics, first `nvidia-smi.txt`: the clocks, what holds them down (power, temperature), the power drawn and its limits, and the temperatures, from one query at the lowest priority; `state.txt` says when it ran, to the millisecond, and the rest is taken 12 s later |
| `after-dv-1` to `after-dv-6` | 5 s and 30 s after each of those videos ends | The kernel log, connectors, properties and GPU state, written at once so that they survive a forced power-off |
| `shutdown` | When the PC is turned off or restarted from the menu on screen | The session's logs, what was connected to each socket, and how the drive and the graphics did second by second |

Turn the PC off from the menu on screen, not with its button, so that `shutdown` is written. Nothing is written while the partition has less than 128 MiB free; a note in `hades-diag`, `NO-ROOM.txt`, then says so. An update with the Windows installer keeps the folder `hades-diag` of the drive it updates, and says what it kept.

On the running system the folder is `/flash/hades-diag`; Kodi's file manager reaches it through *Settings > File manager > Add source > Root filesystem > flash > hades-diag*.

## From a system on an internal disk

Windows does not show the boot partition of a system installed on a disk inside the PC. There are two ways to copy the diagnostics to a USB drive that Windows can read (FAT32, exFAT or NTFS):

- **From the settings:** *LibreELEC settings > System > Submit Log > Copy the diagnostics to a USB drive*, then choose the drive. Only USB drives, removable drives and SD cards that are mounted with writing allowed are offered, never a disk inside the PC.
- **With nothing on the screen:** in Windows, make a folder named `hades-diag` at the top of a USB drive, plug the drive into the PC while Hades runs, wait a minute and pull it out. Where the copy could not be made, a note `NOT-COPIED.txt` in that folder says why.

Either way the copy goes into the folder `hades-diag` of the drive, in a folder named after the date and time, with the sums of its files in `SHA256SUMS.txt`. It is read back from the drive, file by file, before it counts as done, and nothing on the drive is written over.

A folder that an earlier version wrote before its filter took the names of Bluetooth devices out is put through the filter again at the next start with the switch on, and is left out of a copy until then.

## When a display shows nothing

With the diagnostic switch on:

1. Start the PC with the TV or monitor plugged in and turned on, and wait two minutes.
2. Pull its cable out of the PC, wait ten seconds, and plug it back in.
3. Wait three more minutes, and turn the PC off from the menu (on a screen that does work).

The folder of that start then tells what each socket saw at the start (`1-minute`), when the cable went back in (`4-minutes`) and at the end (`shutdown`).

## What the filter takes out

Everything that is written to `hades-diag` goes through a filter first; without the filter nothing is copied. Submit Log uses the same filter.

- Addresses of your network and of the internet (IPv4 and IPv6), MAC and Bluetooth addresses, Wi-Fi network names, the PC's name on the network and the names of the computers of your home network in addresses, the serial numbers of displays (also inside their EDID), of graphics and of USB devices, the UUIDs of disks and partitions, SSH keys and fingerprints, and the names of the Bluetooth devices that the PC knows become markers, such as `[lan-ip-1a2b.3c4d]`, `[mac-5e6f70]` or `[bt-name-7a8b9c]`. The same value gives the same marker on that PC, so a report still shows which lines are about the same device or the same network (the first part of an address's marker is the same for its whole network). A marker cannot be turned back into its value: it is made with a secret that stays on the PC, readable by root only.
- The names of Bluetooth devices are the ones that BlueZ keeps in `/storage/.cache/bluetooth`: the name and the alias of each paired device, and the names in its cache of the devices it saw when it looked for devices (the 64 entries written last; BlueZ keeps none for a device that shows itself under a private address). A keyboard or a phone is often named after its owner (macOS names a Mac's keyboard "Magic Keyboard de Juan"), and the kernel, systemd-logind and Kodi write that name in their logs. Each name is looked for as it is, letter for letter, as a word of its own. A name that is shorter than 6 bytes (a letter beyond A to Z takes two or more), has no letter, or is one plain word of the letters A to Z (such as `TV`, `K` or `Keyboard`) would take ordinary words of the logs with it: it becomes a marker only where a line names an input device by it (the kernel's `input: ... as /devices/...` and `HID ... [...] on`, systemd-logind's `Watching system buttons on ...`, Kodi's `device added: ...`). So does a name that a marker of the filter could hold (only lowercase letters, digits, dots and dashes, such as `bt-name`), and every name after the first 64 (those of the paired devices come first), which keeps the filter within its time on a long log. In `bluetooth.txt` the name and the alias of each paired device are their markers, so that a report still shows which lines of the logs are about which device.
- From the logs of Kodi and of the system, the path of every address goes (only the server's name stays), and so do the names of films, songs and searches (only the extension stays, as in `[redacted].mkv`), e-mail addresses, and anything that looks like a token, a key or a password.
- A line of 32767 characters or more, a line with a run of 256 characters or more, and a line with more than 1024 addresses, names or paths are left out, and say so in their place.

No filter is perfect: look through the files before you give them to anyone. What stays is what tells one PC from another: the names of your add-ons and of your drives, the make and model of your devices, the names of public servers (such as those of the services your add-ons use), which services were listening, and your time zone. Also still there: a short or one-word name of a Bluetooth device where it stands as an ordinary word, the names of Bluetooth devices that BlueZ does not keep (no longer paired, out of its cache, or seen under a private address), user names that add-ons write in their own lines, the names of computers of your network where a line names them outside an address, a folder name with spaces that does not end in the name of a media file, and whatever the firmware's ACPI tables hold, which are copied as they are.

## Submit Log

*LibreELEC settings > System > Submit Log* has two buttons: the latest log, and the latest crash log, each with the configuration. The log holds mainly Kodi's log (or its crash log), the system's journal since it started, what the system sees of the displays and the disks, the boot menus, the Samba configuration and a list of the files of your configuration. Each button asks where it goes, and either way it goes through the filter of the diagnostics ([above](#what-the-filter-takes-out)):

- **Keep it on this system** (the first choice): the log is saved as `/storage/logfiles/system-log-<date>-<time>.txt` (`crash-log-...` for the crash log). To take it off the system, use Kodi's file manager (add the home folder as a source: the log is in its folder `logfiles`), the share "Logfiles" when file sharing (Samba) is on, or SSH. On a system that keeps its storage in memory (live mode), it is gone when the system is turned off.
- **Upload it to the paste service of LibreELEC:** the log goes to `paste.libreelec.tv`, where anyone who has its short address can read it. What that means is explained first, and the question that follows has "No" preselected. Team LibreELEC does not support Hades: give the address only in your report to Hades.

The command `pastekodi`, run over SSH, still uploads the log as it is, unfiltered, as in LibreELEC.

## What to look for in `kodi.log`

Each Bluetooth disconnection is a line `Bluetooth: [mac-...] (kind, IDs) disconnected: org.bluez.Reason...`: what each reason means is in [bluetooth.md](bluetooth.md).

Kodi's log is `/storage/.kodi/temp/kodi.log`; the diagnostics keep its end, and Submit Log takes it whole. The lines of the Dolby Vision output start with `DVBridge:`. The ones that tell most:

| Line | What it tells |
| --- | --- |
| `DVBridge: the first picture of this stream: base ...` | What the stream is: size and layout, whether it has an enhancement layer, the content mapping, how much metadata, where it is drawn |
| `DVBridge: native Dolby Vision output started` | Native output began |
| `DVBridge: native Dolby Vision output disabled: <reason>; the video continues on the ordinary output. ...` | Why native output was not used, and when it is tried again |
| `DVBridge: the display offers Dolby Vision Low-Latency only ...` | The TV input (often one set up for games or a PC) offers only Low-Latency: Dolby Vision Standard is not available there |
| `DVBridge: the display reads Dolby Vision display management N: frames carry the content mapping v2.9 part ...` | A TV of display management 2 or 3: it is sent only the part of the metadata that it reads |
| `DVBridge: last 10 s: N frames shown, N not shown, ...` | Every 10 s during a video: frames shown and missed, preparation and presentation times, frames presented a frame ahead and refreshes missed, GPU time and the fidelity step-down level; with `<dvpasstimes>` in `advancedsettings.xml` ([settings.md](settings.md#advancedsettingsxml)), also the passes that took the most of that GPU time |
| `DVBridge: frames are now presented a frame ahead` | The frame-ahead presentation is on for this video (`no longer` when it goes off). Before it, `DVBridge: frames presented the way that waits missed their refresh again and again ...` where missed refreshes called for it |
| `DVBridge: the content mapping v4.0 part of the metadata of this stream cannot be carried: ...` | The frames of that stream go out with the v2.9 part of their metadata only, as to a display of display management 2 or 3, and keep native output |
| `DVBridge: native Dolby Vision entered with the output turned off and on again on connector ...` | A dual-mode DisplayPort connector of NVIDIA: native output started in the mode that the output has, with its link set up from scratch |
| `DVBridge: the fidelity step-down of native Dolby Vision frames is forced to level N ...` | `advancedsettings.xml` forces a level ([settings.md](settings.md#advancedsettingsxml)): above level 0 the picture is not the faithful one |
| `DVBridge: P5 frames with GUI elements: ...` | Profile 5 with menus over it on graphics that do not keep up: how the pixels the menus cover are converted from now on, and when that goes back |
| `DVBridge: the pixels that the GUI covers in this stream are converted back by the transport itself, as before: ...` | Profile 5 with menus over it: the graphics could not make the pass that converts those pixels once, so they cost more GPU time; the bytes are the same |
| `DVBridge: the vblanks of the output cannot be waited for (...)` | The driver has no vertical blank to wait for (nvidia-drm with its defaults): a picture that repeats after a frame presented the way that waits is kept by committing it again, and the 10-second lines count how many were |
| `DVBridge: native Dolby Vision output goes on into the mode ...` | The next video took the native output over in its own mode, without restoring the ordinary output first |
| `DVBridge: native Dolby Vision output ended, normal output restored` | The end of a native Dolby Vision video |
| `DVBridge: Dolby Vision reconstructed for output as HDR10: ...` | Dolby Vision is being shown as HDR10 |
| `DVBridge: Dolby Vision is rebuilt and mapped to an output that takes no HDR10: ...` and `DVBridge: an HDR picture (HDR10 or HLG) is mapped to an output that takes no HDR: ...` | A display without HDR: what the picture is mapped to, with the reference tone mapping (BT.2390) |
| `DVBridge: the gamut mapping table is the standard one, 96x64x512 in rgba16, 24 MiB ...` | Where a picture is mapped to the display or to SDR: the size of the table of the gamut mapping, its format (`rgba32f` where the graphics chip filters no 16-bit unorm texture) and its memory; the finer one where "Finer gamut mapping" asks for it, or that it could not be made. Where the chip filters none of the formats, a warning says that the gamut is mapped by saturation |
| `DVBridge: Dolby Vision Low-Latency is mapped to the display by LibreELEC Hades (not by Dolby's display management): to N cd/m2 and the primaries ...` | Dolby Vision Low-Latency is in use, and the peak and primaries the picture is mapped to |
| `DV: its Dolby Vision metadata are made from its HDR10+ metadata, picture by picture` and `DV: its Dolby Vision metadata are measured from the picture as it plays` | An HDR10+ or an HDR10 video that a setting sends as Dolby Vision. With "(not now: ...)" at the end, it plays as it is |
| `DVBridge: HDR10 measured as Dolby Vision: N of the last 240 pictures not measured in time, N scene cuts; ...` | Every 240 pictures of an HDR10 video sent as Dolby Vision: pictures measured too late, cuts found, and the metadata sent last |
| `HDR10+: the dynamic metadata of each frame goes out with it ...` | HDR10+ is sent with each frame. Other `HDR10+:` lines say why it is not, and when a stretch without metadata goes out as HDR10. Where the driver refused it, the kernel log says why: `Hades: HDR10+ refused on head N: ...` on NVIDIA, `HDR10+ refused: ...` on Intel |
| `HDR output: ... goes out ...` | The format and bit depth that the driver says it sends HDR10 or HLG in, and at 8 bits why (the link carries no more, or "8-bit RGB" is chosen). `DVBridge: Dolby Vision rebuilt as HDR10 goes out ...` says the same for Dolby Vision shown as HDR10 |
| `HDR output: no HDR signalling for this ... video: ...` | An HDR video that goes out without HDR signalling, and why (for example, the display does not declare its transfer function) |
| `... is open for PCM: N channels ...` or `... is open for passthrough: ...` | What the audio output was opened with: for PCM the channels, their layout, the rate and the format; for passthrough the stream type |
| `Using connector: ...` | The display output that Kodi chose at start, and in another line which mode it starts in and where that mode comes from |
| `Output depth: ...` | On NVIDIA and Intel over HDMI: at how many bits SDR and HDR go out (`"max bpc"`), and with which change of mode it changes. The kernel log says how the link went out (on NVIDIA, a line `Hades: head N (...): ... HDMI TMDS at ... kHz` or `HDMI FRL ...` for each HDMI mode set) |
| `Modes: the display of ... has other modes (...)` | The display offered other modes while Kodi ran: the resolutions were made again of them, and which one the interface took |
| `ActiveAE - no audio output has been chosen in the settings: using ..., the output of the display that Kodi uses, ...` | The audio output that Hades took for the display in use. `... was chosen in the settings: it is kept` where you chose one |
| `CDVDInputStreamBluray::Open - BD-J - ...` | Whether Blu-ray Java menus can run: `Blu-ray Java menus can run: ...`, or what is missing (the add-on "JRE for BD-J menus", a restart of Kodi after installing it, or the Java part of the libbluray version of this system) |
| `Monitor: ...` | A change of the Monitor setting while Kodi runs: where Kodi moved, why it could not, or that the change waits for the next start |
| `Wake: ...` | After a wake from suspend: whether the output was found off and its mode set again |
| `GUI: in the last N s, N passes took 50 ms or more with no video playing ...` | Once a minute, only when the interface was slow: the slow passes and the longest with its window, how long add-ons held the interface, and how many motions of the mouse were left for the one that followed them; with a video playing, how many passes took more than 1.5 refresh periods, and in which window |
| `GUI: N windows that open over a video were loaded ahead while the menus were idle, ...` | Once after the menus were idle: the windows loaded ahead and how long each took |
| `CApplicationPlay: the server of the stream gave its type (...) in N ms, ...` | Before a stream without a type plays, how long its server took to say what it is |

## The player information

Two pages of the player information tell most during a video:

- **The overview page:** for HDR output, the bits per component that the driver says it sends (on Intel and AMD also the format, for example "10 bits, YCbCr 4:2:0"); for SDR, the most that "max bpc" lets through. A "GPU" row gives the use and the temperature that the driver of the GPU reports, read only while the page is open.
- **The Dolby Vision page:** what the stream is and, when the output is not native Dolby Vision, why. With native output it also shows the average GPU time of a frame in the last 10 s and the headroom that leaves of a refresh period (negative: the frames take longer than a refresh), how the last frame without menus was drawn (direct or composed), whether frames are presented a frame ahead, and the fidelity step-down level.

The Dolby Vision page is itself a menu over the video: while it is open the frames are composed, and drawing it is part of the GPU time it shows. The 10-second lines of `kodi.log` also give the time of the frames without menus, and how many repeated pictures were drawn again, and why. With a skin other than Estuary, a second press of the key that opens the player process information opens these pages, where the skin has no such pages of its own.
