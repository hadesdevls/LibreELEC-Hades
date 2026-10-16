# Diagnostics and logs

What to send with a problem report, and how to get it. "Reporting problems" in the [README](../README.md#reporting-problems) says what else a report needs.

## The diagnostic switch

Off by default. While it is on, every start of the PC leaves a record of what the system saw on the drive's boot partition: the one Windows shows as **LIBREELEC** when the drive is plugged into it.

- **Turning it on:** tick "Save diagnostics there" in the Windows installer when you install or update; or, on a drive that is plugged into a Windows PC, rename the file `DIAG-OFF.TXT` at the top of that partition to `DIAG-ON.TXT`.
- **Turning it off:** rename it back, or update with the box unticked.
- **Where it goes:** the folder `hades-diag` on that partition. Kodi's file manager reaches it too (*Settings > File manager > Add source > Root filesystem > flash > hades-diag*).

Each start of the PC has a folder of its own, `start-1`, `start-2` and so on; the last eight are kept, so a drive taken from one PC to the next keeps what each of them had to tell.

| Folder | When | What |
| --- | --- | --- |
| `1-minute`, `4-minutes` | About 1 and 4 minutes after Kodi starts | The graphics and what is connected to each (with the EDID of every display), the kernel log, the end of `kodi.log`, the system's warnings, sound and USB devices, network state, services, the clock. The first also asks the empty HDMI and DisplayPort sockets once more, lists the PC's devices and keeps the firmware's ACPI tables (not the one that holds a Windows product key) |
| `during-dv-1` ... `during-dv-6` | About 15 s after each Dolby Vision video starts | What is really sent to the TV: the connector's properties with the Dolby InfoFrame, the driver's atomic state |
| `after-dv-1` ... `after-dv-6` | 5 s and 30 s after each Dolby Vision video ends | The kernel log, connectors, properties and GPU state, written at once so they survive a forced power-off |
| `shutdown` | When the PC is turned off or restarted from the menu on screen | The session's logs, what was connected to each socket, and how the drive and the graphics did second by second |

Nothing is written when the partition has less than 128 MiB free (a note, `NO-ROOM.txt`, then says so). Turn the PC off from the menu on screen, not with its button, so that the `shutdown` folder is written.

**Privacy.** Before a log of Kodi or of the system's services is written there, it goes through a filter that removes the path of every address (only the server's name stays), e-mail addresses, and anything that looks like a token, a key or a password; without the filter no such log is copied. The kernel log is written as it is. No filter is perfect: look at the files before you give them to anyone. What stays is what tells one PC from another: the names of your add-ons and of what you played, the make and model of your devices (and the serial numbers they report), the PC's network name and addresses, your time zone. **Nothing is sent anywhere**: the files stay on the drive. The folder has a `README.txt` that says the same.

## Submit Log

*LibreELEC settings > System > Submit Log* works as in LibreELEC, with one difference: before anything is uploaded, it asks. By default the log is **saved on the system**, through the same filter, in `/storage/logfiles` (reachable with Kodi's file manager: *Root filesystem > storage > logfiles*). Uploading to LibreELEC's paste service is still offered, with what that means explained first; "No" is preselected.

## What to look for in `kodi.log`

The lines of the Dolby Vision output start with `DVBridge:`. The ones that tell most:

| Line | Meaning |
| --- | --- |
| `DVBridge: the first picture of this stream: base ...` | What the stream is: size and layout, whether it has an enhancement layer, the content mapping, how much metadata, where it is drawn |
| `DVBridge: native Dolby Vision output started` | Native output began |
| `DVBridge: native Dolby Vision output disabled: <reason>; the video continues on the ordinary output. ...` | Why native output was not used, and when it will be tried again |
| `DVBridge: the display offers Dolby Vision Low-Latency only ...` | The TV input is set up for games or a PC: Dolby Vision Standard is not available there |
| `DVBridge: last 10 s: N frames shown, N not shown, ...` | Every 10 s during a video: frames shown and missed, preparation and presentation times, frames presented a frame ahead and refreshes missed, GPU time and the fidelity step-down level of the frames it is from |
| `DVBridge: the fidelity step-down of native Dolby Vision frames is forced to level N ...` | `advancedsettings.xml` forces a level ([settings.md](settings.md)): the picture is not the faithful one above level 0 |
| `DVBridge: frames are now presented a frame ahead` | The frame-ahead presentation is on for this video |
| `DVBridge: Dolby Vision reconstructed for output as HDR10: ...` | Dolby Vision is being shown as HDR10 |
| `DVBridge: native Dolby Vision output ended, normal output restored` | The end of a native Dolby Vision video |
| `Using connector: ...` | The display output that Kodi chose at start |
| `HDR output: HDR10 goes out as YCbCr 4:2:0 at 10 bits in ...` | On Intel and AMD, the format and depth the driver sends HDR10 or HLG in, as it answers when asked once a video's HDR signalling is on (`DVBridge: Dolby Vision rebuilt as HDR10 goes out ...` for Dolby Vision shown as HDR10). At 8 bits it says why: the link carries no more, or 8-bit RGB is chosen ([settings.md](settings.md)) |

On Intel and AMD the overview page of the player information gives, for HDR output, the bits per component and the format that the driver says it sends (for example "10 bits, YCbCr 4:2:0"); elsewhere, and for SDR, the most that "max bpc" lets through. The player information also has a **Dolby Vision page**: what the stream is and, when the output is not native Dolby Vision, why. With native output it also shows the average GPU time of a frame in the last 10 s and the headroom that leaves of a refresh period (negative: the frames take longer than a refresh), how the last frame without menus was drawn (direct or composed), whether frames are presented a frame ahead, and the fidelity step-down level. The page is itself a menu over the video: while it is open the frames are composed, and drawing it is part of the GPU time it shows. The 10-s lines of `kodi.log` also give the time of the frames without menus.
