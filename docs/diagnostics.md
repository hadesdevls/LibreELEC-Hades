# Diagnostics and logs

What to send with a problem report, and how to get it. "Reporting problems" in the [README](../README.md#reporting-problems) says what else a report needs.

## The diagnostic switch

Off by default. While it is on, every start of the PC leaves a record of what the system saw on the drive's boot partition: the one Windows shows as **LIBREELEC** when the drive is plugged into it.

- **Turning it on:** tick "Save diagnostics there" in the Windows installer when you install or update; or, on a drive that is plugged into a Windows PC, rename the file `DIAG-OFF.TXT` at the top of that partition to `DIAG-ON.TXT`.
- **Turning it off:** rename it back, or update with the box unticked.
- **Where it goes:** the folder `hades-diag` on that partition. Kodi's file manager reaches it too (*Settings > File manager > Add source > Root filesystem > flash > hades-diag*).
- **From a system on a disk inside the PC**, whose boot partition Windows does not show: *LibreELEC settings > System > Submit Log > Copy the diagnostics to a USB drive*; or, with nothing on the screen, make a folder named `hades-diag` at the top of a USB drive in Windows, plug the drive into the PC while Hades runs, wait a minute and pull it out. Either way the copy goes into the folder `hades-diag` of the drive, in a folder named after the date and time, is read back from the drive file by file (its sums are in `SHA256SUMS.txt`), and nothing on the drive is written over. When the drive with the folder cannot take it, a note `NOT-COPIED.txt` in that folder says why. An update with the installer keeps the folder `hades-diag` of the drive it updates, and says what it kept.

Each start of the PC has a folder of its own, `start-1`, `start-2` and so on; the last eight are kept, so a drive taken from one PC to the next keeps what each of them had to tell.

| Folder | When | What |
| --- | --- | --- |
| `1-minute`, `4-minutes` | About 1 and 4 minutes after Kodi starts | The graphics and what is connected to each (with the EDID of every display), the kernel log, the end of `kodi.log`, the system's warnings, sound and USB devices, network state, services, the clock. The first also asks the empty HDMI and DisplayPort sockets once more, lists the PC's devices and keeps the firmware's ACPI tables (not the one that holds a Windows product key) |
| `during-dv-1` ... `during-dv-6` | About 15 s after each Dolby Vision video starts | What is really sent to the TV: the connector's properties with the Dolby InfoFrame, the driver's atomic state |
| `after-dv-1` ... `after-dv-6` | 5 s and 30 s after each Dolby Vision video ends | The kernel log, connectors, properties and GPU state, written at once so they survive a forced power-off |
| `shutdown` | When the PC is turned off or restarted from the menu on screen | The session's logs, what was connected to each socket, and how the drive and the graphics did second by second |

Nothing is written when the partition has less than 128 MiB free (a note, `NO-ROOM.txt`, then says so). Turn the PC off from the menu on screen, not with its button, so that the `shutdown` folder is written.

**Privacy.** Everything that is written there goes through a filter first; without the filter nothing is copied.

- Addresses of your network and of the internet (IPv4 and IPv6), MAC and Bluetooth addresses, Wi-Fi network names, the PC's name on the network and the names of the computers of your home network in addresses, the serial numbers of displays (also inside their EDID), graphics cards and USB devices, the UUIDs of disks and partitions, and SSH keys and fingerprints become markers, such as `[lan-ip-1a2b.3c4d]` or `[mac-5e6f70]`. The same value gives the same marker on that PC, so a report still shows which lines are about the same device or the same network (the first part of an address's marker is the same for its whole network), but a marker cannot be turned back into its value: it is made with a secret kept on the PC (`/storage/.cache/hades-diag/key`, readable by root only), which never goes into the diagnostics.
- In the logs of Kodi and of the system, the path of every address goes (only the server's name stays), and so do the names of films, songs and searches (only the extension stays, as in `[redacted].mkv`), e-mail addresses, and anything that looks like a token, a key or a password.
- A line of 32767 characters or more, or with more than 1024 addresses, names or paths in it, is left out, and says so in its place.

No filter is perfect: look at the files before you give them to anyone. What stays is what tells one PC from another: the names of your add-ons and of your drives, the make and model of your devices, the names of public servers, which services were listening, and your time zone. Also still there: the names of Bluetooth devices, user names that add-ons write in their own lines, the names of computers of your network where a line names them outside an address, a folder name with spaces that does not end in the name of a media file, and whatever the firmware's ACPI tables hold, which are copied as they are and were not looked through. **Nothing is sent anywhere**: the files stay on the drive. The folder has a `README.txt` that says the same.

## Submit Log

*LibreELEC settings > System > Submit Log* works as in LibreELEC, with two differences: before anything is uploaded, it asks; and the log goes through the same filter as the diagnostics, whether it is kept or uploaded. By default the log is **saved on the system** in `/storage/logfiles` (reachable with Kodi's file manager: *Root filesystem > storage > logfiles*). Uploading to LibreELEC's paste service is still offered, with what that means explained first; "No" is preselected. The command `pastekodi`, run over SSH, still uploads the log as it is, as in LibreELEC.

## What to look for in `kodi.log`

The lines of the Dolby Vision output start with `DVBridge:`. The ones that tell most:

| Line | Meaning |
| --- | --- |
| `DVBridge: the first picture of this stream: base ...` | What the stream is: size and layout, whether it has an enhancement layer, the content mapping, how much metadata, where it is drawn |
| `DVBridge: native Dolby Vision output started` | Native output began |
| `DVBridge: native Dolby Vision output disabled: <reason>; the video continues on the ordinary output. ...` | Why native output was not used, and when it will be tried again |
| `DVBridge: the display reads Dolby Vision display management N: frames carry the content mapping v2.9 part ...` | A TV of display management 2 or 3: the content mapping v4.0 blocks are left out of the metadata it is sent |
| `DVBridge: the display offers Dolby Vision Low-Latency only ...` | The TV input is set up for games or a PC: Dolby Vision Standard is not available there |
| `DVBridge: last 10 s: N frames shown, N not shown, ...` | Every 10 s during a video: frames shown and missed, preparation and presentation times, frames presented a frame ahead and refreshes missed, GPU time and the fidelity step-down level of the frames it is from |
| `DVBridge: the fidelity step-down of native Dolby Vision frames is forced to level N ...` | `advancedsettings.xml` forces a level ([settings.md](settings.md)): the picture is not the faithful one above level 0 |
| `DVBridge: frames are now presented a frame ahead` | The frame-ahead presentation is on for this video |
| `DVBridge: Dolby Vision reconstructed for output as HDR10: ...` | Dolby Vision is being shown as HDR10 |
| `DVBridge: Dolby Vision is rebuilt and mapped to an output that takes no HDR10: ...` and `DVBridge: an HDR picture (HDR10 or HLG) is mapped to an output that takes no HDR: ...` | A display without HDR: what the picture is mapped to, with the reference tone mapping (BT.2390) |
| `DVBridge: Dolby Vision Low-Latency is mapped to the display by LibreELEC Hades (not by Dolby's display management): to N cd/m2 and the primaries ...` | Dolby Vision Low-Latency is in use, and the peak and primaries the picture is mapped to |
| `DV: its Dolby Vision metadata are made from its HDR10+ metadata, picture by picture` and `DV: its Dolby Vision metadata are measured from the picture as it plays` | An HDR10+ or an HDR10 video that a setting sends as Dolby Vision. With "(not now: ...)" at the end, it plays as it is |
| `DVBridge: HDR10 measured as Dolby Vision: N of the last 240 pictures not measured in time, N scene cuts; ...` | Every 240 pictures of an HDR10 video sent as Dolby Vision: pictures measured too late, cuts found, and the metadata sent last |
| `HDR10+: the dynamic metadata of each frame goes out with it ...` | HDR10+ is sent with each frame. Other `HDR10+:` lines say why it is not (the driver has no property for it on that connector, or did not take it) and when a stretch without metadata goes out as HDR10. `HDR10+: the driver refused the dynamic metadata in the test of N commits (...)` means that its reason is in the kernel log: `Hades: HDR10+ refused on head N: ...` on NVIDIA, `HDR10+ refused: ...` on Intel |
| `DVBridge: the vblanks of the output cannot be waited for (...)` | The driver has no vertical blank to wait for (nvidia-drm with its defaults): a picture that repeats is drawn again every refresh |
| `DVBridge: native Dolby Vision output goes on into the mode ...` | The next video took the native output over in its own mode, without restoring the ordinary output first |
| `DVBridge: native Dolby Vision output ended, normal output restored` | The end of a native Dolby Vision video |
| `DVBridge: the pixels that the GUI covers in this stream are converted back by the transport itself, as before: ...` | Profile 5 with the GUI over it: the GPU could not make the pass that converts those pixels once, so they cost more GPU time; the bytes are the same |
| `Using connector: ...` | The display output that Kodi chose at start |
| `Monitor: ...` | A change of the Monitor setting while Kodi runs: where Kodi moved, why it could not, or that the change waits for the next start |
| `GUI: in the last N s, N passes took 50 ms or more with no video playing ...` | Once a minute, only when the interface was slow: the slow passes and the longest with its window, how long add-ons held the interface, and how many motions of the mouse were left for the one that followed them |
| `CApplicationPlay: the server of the stream gave its type (...) in N ms, the interface going on meanwhile` | Before a stream without a type plays, how long its server took to say what it is |
| `Wake: ...` | After a wake from suspend: whether the output was found off and its mode set again |
| `HDR output: HDR10 goes out as YCbCr 4:2:0 at 10 bits in ...` | On Intel and AMD, the format and depth the driver sends HDR10 or HLG in, as it answers when asked once a video's HDR signalling is on (`ycbcr422` on AMD where it sends 4:2:2; `DVBridge: Dolby Vision rebuilt as HDR10 goes out ...` for Dolby Vision shown as HDR10). On NVIDIA, `HDR output: HDR10 goes out at N bits in ..., as the driver says (NV_OUTPUT_BPC)`. At 8 bits it says why: the link carries no more, or 8-bit RGB is chosen ([settings.md](settings.md)) |
| `HDR output: no HDR signalling for this ... video: ...` | An HDR video that goes out without HDR signalling, and why (for example, the display does not declare its transfer function) |
| `... is open for PCM: N channels ...` or `... is open for passthrough: ...` | What the audio output was opened with: for PCM the channels, their layout, the rate and the format; for passthrough the stream type |

On Intel, AMD and NVIDIA the overview page of the player information gives, for HDR output, the bits per component (and on Intel and AMD the format) that the driver says it sends (for example "10 bits, YCbCr 4:2:0"); for SDR, the most that "max bpc" lets through. It also has a "GPU" row: the use and the temperature that the driver of the GPU gives, read only while the page is open. The player information also has a **Dolby Vision page**: what the stream is and, when the output is not native Dolby Vision, why. With native output it also shows the average GPU time of a frame in the last 10 s and the headroom that leaves of a refresh period (negative: the frames take longer than a refresh), how the last frame without menus was drawn (direct or composed), whether frames are presented a frame ahead, and the fidelity step-down level. The page is itself a menu over the video: while it is open the frames are composed, and drawing it is part of the GPU time it shows. The 10-s lines of `kodi.log` also give the time of the frames without menus, and how many repeated pictures were drawn again, and why. With a skin other than Estuary, a second press of the key that opens the player process information opens the overview and Dolby Vision pages, where the skin has no such pages of its own.
