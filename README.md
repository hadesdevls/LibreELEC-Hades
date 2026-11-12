# LibreELEC Hades

LibreELEC Hades is an unofficial build of [LibreELEC](https://libreelec.tv) for 64-bit PCs (the "Generic x86_64" image). LibreELEC turns a PC into a media centre that runs Kodi and little else. Hades adds what a home cinema asks of it: Dolby Vision sent from the PC to the TV over HDMI in the TV-led way that Dolby Vision players use, including profile 7 with its full enhancement layer, HDR10+, and Dolby Atmos and the other HD sound formats passed to your receiver untouched. Everything else that LibreELEC does is kept.

> **This is not LibreELEC and it is not official.** LibreELEC Hades is not made, supported, endorsed or certified by Team LibreELEC, Team Kodi or the XBMC Foundation, Dolby Laboratories, HDR10+ Technologies, NVIDIA, AMD or Intel, and has no affiliation with any of them. Please do not ask them for help with it. Kodi is a registered trademark of the XBMC Foundation. Dolby Vision and HDR10+ are named here only as video formats: LibreELEC Hades uses neither logo and is certified by neither. Other trademarks belong to their owners; see [NOTICE.md](NOTICE.md).

**Why the screen says "LibreELEC Hades".** The media centre of this build is Kodi with changes that the XBMC Foundation has not approved, and Kodi's trademark policy asks such a build not to use Kodi's name and logos as its own. Underneath, it is Kodi: add-ons, skins and remote-control apps work as with Kodi, its log is still `kodi.log`, and other programs still see Kodi. The page "About this build" of the system information says what it is and how to support Kodi and LibreELEC.

**Version 1.0.** The first version that is not a release candidate. Native Dolby Vision, including profile 7 with its full enhancement layer, has been seen working on NVIDIA, AMD and Intel graphics, with Dolby Atmos passed through. What is new since 1.0 RC8 is listed in the [changelog](CHANGELOG.md), and what nobody has seen on a screen yet is listed under [Not tried on hardware yet](#not-tried-on-hardware-yet). If you try Hades, please tell me what you see in the issues: results from more hardware are what this project needs most.

## Start here

- **You want to try it.** Read [What you need](#what-you-need), then [Installing and starting](#installing-and-starting). On Windows it takes a few minutes and changes nothing in Windows.
- **You have it and want to set it up.** Most people change nothing: Dolby Vision is on by default. *Check this setup* plays test pictures and sounds and tells you what reached your TV and which sound formats to switch on ([Your first Dolby Vision video](#your-first-dolby-vision-video)), and [the settings guide](docs/settings.md) says what each setting does and when to touch it.
- **You want to know what Hades adds.** [Extras of Hades](docs/features.md) explains each one in plain words, with an example and the setting that controls it.
- **You want to know how it works.** [How it works](docs/how-it-works.md) follows a video from the file to the TV, plainly first and in technical detail further down.
- **Something is wrong.** See [Reporting problems](#reporting-problems): one button makes a diagnostic report with the files that let me find the cause, and the [diagnostics](docs/diagnostics.md) guide says what is in it.

## What it adds to LibreELEC

- **Native Dolby Vision over HDMI.** Each picture is rebuilt as the Dolby Vision specification defines it, including the full enhancement layer (FEL) of profile 7, and sent with its metadata for every frame. The TV does its own Dolby Vision processing, as with a certified player (its "Standard" or "TV-led" mode). Profiles 5, 7 (FEL and MEL) and 8.1.
- **NVIDIA, AMD and Intel graphics,** graphics cards and integrated graphics alike (NVIDIA with its open kernel modules, and the GeForce GTX 900 and GTX 10 cards and the TITAN V with NVIDIA's driver 580).
- **A safe fallback.** Dolby Vision is used only when the video, the TV, the HDMI link and the graphics driver all qualify. Otherwise the video plays the ordinary way, as HDR10 or SDR, and the first page of the player information says why in one sentence.
- **A player information page that a person can read.** Press O on a keyboard, or the red key of a TV remote that sends keys over HDMI-CEC. It says whether the video plays as intended, what the file carries, what Hades does with it and what reaches your TV.
- **A check of your setup.** *Check this setup* plays test pictures in SDR, HDR10, HLG and Dolby Vision, each speaker in turn and a chime in each sound format, says what went out, asks what you saw or heard, and switches on the sound formats that you heard.
- **The refresh rate Kodi would choose,** exact for progressive video (25 fps at 25 Hz, 29.97 fps at 29.97 Hz). On NVIDIA, the modes at exactly 24, 30, 60 and 120 Hz that the driver did not offer are added. DVD films stored as interlaced video are put back together picture by picture and shown at their own rate.
- **Picture and sound in step:** each frame is shown when its sound plays, measured on your display, and passthrough sound (TrueHD, Dolby Digital Plus, DTS) is kept through a pause.
- **Made for integrated graphics too.** A frame that takes most of a refresh period is prepared one refresh ahead, and the picture under menus and subtitles is redrawn only where they are.
- **HDR10+** with the dynamic metadata of each frame, where the graphics driver can send it.
- **HDR sent more exactly:** on Intel and AMD graphics at 3840x2160 and 50 or 60 Hz over HDMI 2.0, HDR10, HLG and HDR10+ go out as YCbCr 4:2:2 at 12 bits inside the signal that carries Dolby Vision, where the TV takes it, so that the menus and subtitles over the video keep their colour detail; on NVIDIA, where the card would send HDR in RGB at 8 bits, it goes out as YCbCr 4:2:2 at 12 bits (10 on the GeForce GTX 10 cards and the TITAN V).
- **Zoom, vertical shift and stretching** keep native Dolby Vision.
- **Dolby Vision shown as HDR10** where native output is not possible: always for profile 5, whose base picture cannot be watched on its own, and for profile 7 if you turn it on. A display without HDR gets HDR mapped to SDR with the reference method of ITU-R BT.2390, which keeps everything the display can show as it was mastered, for a reference display, a bright room, a projector or the white and gamma of your own display.
- **Options, off by default:** Dolby Vision for TVs in game mode (Low-Latency), for a TV input that offers only that, mapped to the TV by Hades; Dolby Vision with the older HDMI 1.4b signalling, for the TVs of 2016 that ask for it; HDR10+ or HDR10 sent as Dolby Vision, and Dolby Vision sent as HDR10+; HLG sent as Dolby Vision (on its own where the output cannot send HLG); for projectors, Dolby Vision without its trims or as Low-Latency mapped to a peak set by hand, HDR10 and HDR10+ mapped to the display (HDR10+ by its own metadata), the black of the display, and a limit on the luminance that HDR10 tells it; HDR videos played as SDR, for a TV that shows HLG badly.
- **3D Blu-ray video** (H.264 MVC): both views are decoded, and a 3D display is switched to 3D over HDMI.
- **Blu-ray Java (BD-J) menus with nothing to install,** in a service of the system kept apart from Kodi, with no network unless BD-Live is turned on, and then the Internet only.
- **PCs with two graphics chips, and laptops:** Kodi starts on the TV, whichever chip it is connected to, and moves to a TV connected later (on the same graphics without a restart).
- **Streams that keep going:** a network or a streaming service that stops sending for a while no longer ends the film; it goes on from where it stopped.
- **Safer defaults, a Windows installer, and a diagnostic report made with one button** (see below).

[Extras of Hades](docs/features.md) explains each of these, and the smaller ones that are not listed here.

## What you need

- A 64-bit PC with Secure Boot turned off (Hades does not start with it yet).
- A USB drive of 8 GB or more. A USB SSD runs much more smoothly than a stick.
- For Dolby Vision:
  - **A TV that offers Dolby Vision "Standard" on the HDMI input you use.** Many TVs offer only low-latency Dolby Vision on an input set up for games or a PC; Hades says so and sends that input HDR10, or low-latency Dolby Vision if you turn that on. On an LG C5, "Quick Frame Transport" has to be off on that input.
  - **Supported graphics:** NVIDIA GeForce GTX 16, RTX 20 or newer, and with NVIDIA's driver 580 the GeForce GTX 900 and GTX 10 cards and the TITAN V; AMD Radeon graphics with display hardware DCN 1.0 or newer, from Raven, Picasso and Navi 1x to the RX 9000 series and the newest Ryzen processors (DCN 4.2 and 4.2B, new in 1.0), and, since 1.0 RC5, the Radeon RX 400 and 500 series, the Radeon RX Vega M, the Radeon RX Vega 56 and 64 and the Radeon VII (display hardware DCE 11.2 and 12; not older AMD hardware, nor the few versions listed in [docs/how-it-works.md](docs/how-it-works.md#graphics-drivers)); Intel graphics of display version 9 or newer (UHD 6xx and later). On the Polaris and Vega cards the Dolby Vision path is off unless you turn it on, and Dolby Vision plays as HDR10 there (see [Known limits](#known-limits)).
  - **The PC connected by HDMI straight to the TV** is best, from an HDMI port of the graphics or through a passive DisplayPort adaptor (DP++) that identifies itself as HDMI. A sound bar or AV receiver in between has to pass Dolby Vision through.
- Your own videos or add-ons: Hades comes with no films, add-ons or media sources.

## Installing and starting

You need the image file (`LibreELEC-Generic.x86_64-...img.gz`), from the [releases](https://github.com/hadesdevls/LibreELEC-Hades/releases) of this repository or built yourself. The Windows installer is in the same place and comes with its own README.

1. **Write the image to a USB drive.** On Windows, run `LibreELEC-Hades-Installer.exe`: it lists your drives with their model, size and serial number, asks you to type a word before it touches anything, and checks what it wrote. Any other program that writes disk images works too. The drive is erased.
2. **Start the PC from the drive.** Restart the PC, open the firmware's boot menu (F8, F11, F12 or Esc on most PCs) and choose the USB drive.
3. **Choose "Run" in the boot menu** (it starts by itself after 5 seconds). The first start prepares the storage and restarts once. Choose "Run" again.
4. **Answer the first-run wizard** that LibreELEC shows: language, the name of the system, the network and the services. If you want to copy files to the system over the network, turn Samba on here; the wizard then shows the address, the user and the password that you need ([Updating](#updating) has the details). SSH is a separate switch.

What the boot menu offers:

| Entry | What it does |
| --- | --- |
| Run (recommended) | The normal start. The PC's internal disks stay hidden from Hades, and each device reaches only its own memory. |
| Run (internal disks read-only) | Shows the internal disks, mounted read-only, and starts devices as LibreELEC does. |
| Live | Settings and add-ons stay in memory only and are gone at power off. |
| Installer | Installs Hades on a disk that you choose, erasing it. It asks twice first, and it is never the entry that starts by itself. |

To go back to your usual system, shut Hades down and start the PC normally. Started from USB, Hades changes nothing in the PC's boot settings or in Windows. Installing on an internal disk, instead of a USB drive, is also possible: the Windows installer explains the two ways (making room without erasing anything, or using a whole secondary disk, never the one Windows starts from).

## Your first Dolby Vision video

1. Play a Dolby Vision file. When the TV switches to its Dolby Vision mode, Hades is sending Dolby Vision. The TV's Dolby Vision logo alone does not prove that the colours are right: check the picture too.
2. Press O on a keyboard, or the red key of a TV remote that works over HDMI-CEC, to open the player information. Its first page has one sentence that says whether the video plays as intended and, if not, what is wrong and what to do about it.
3. If you would like a short notice each time a video starts, turn on *Settings > System > Display > Show the playback mode when a video starts*. It says, for example, "Dolby Vision, FEL, 3840x2160 at 23.976 Hz".
4. If the picture is not Dolby Vision where you expect it, the player information says why, and [Known limits](#known-limits) lists the usual reasons. Most of them are about the TV input (game mode) or the connection (a converter or a receiver in between).
5. Not sure? *Settings > LibreELEC > System > Check this setup* plays a test picture in SDR, HDR10, HLG and Dolby Vision, a sound from each speaker and a chime in each sound format, says what went out, asks what you saw or heard and says what to do. It switches on the sound formats that you heard, and changes nothing else.

You do not need to change any setting to watch Dolby Vision. [The settings guide](docs/settings.md) starts with the few that people do change, such as the brightness of the menus on an HDR TV, and has a section for projectors.

## Updating

Your settings, add-ons and the diagnostics folder are kept whichever way you update. The new version is installed by the system itself at the next start, and the system restarts on its own when it is done. The boot menu does not wait for you after that restart: the one start that applies an update goes straight on, and the menu is back, as it was, from the next start.

- **With the Windows installer:** choose the drive that has Hades and "Update". It writes the new system and keeps everything else.
- **As in LibreELEC, with the `.tar` file** of the new version: copy it to the `Update` folder (`/storage/.update`) and restart. Over the network that folder is a share of the system, and file sharing (Samba) is off in Hades until you turn it on:
  1. In *Settings > LibreELEC > Services* turn Samba on. A window shows the name of the system, the user (`libreelec`) and the password that Hades generates for it; the same page keeps them if you need them again.
  2. On your computer open `\\<name of the system>\Update` (Windows) or `smb://<name of the system>/Update` (Mac, Linux), and sign in with that user and password.
  3. Copy the `.tar` file there and restart the system.
- **SSH is a separate switch.** While root still has LibreELEC's default password, SSH only accepts keys: set a password first (the first-run wizard asks for it when you turn SSH on) if you want to log in with one.

Hades does not look for updates by itself and does not contact LibreELEC's update server, because an official LibreELEC update would remove Dolby Vision. *Settings > LibreELEC > Updates > Available Versions* says where the versions of Hades are published.

## Defaults that differ from LibreELEC

Nothing from LibreELEC is removed. LibreELEC is made for a box that does nothing else. A PC that also holds your other disks and your Windows needs more care, so Hades starts with safer values for what is unsafe by default. Each one can be turned back on, and each is explained here.

### Your PC's disks

- **Started from USB, the internal disks are hidden.** In the "Run" and "Live" entries Hades does not start any internal storage controller, whatever its kind, until the next start ([how](docs/settings.md#outside-the-settings-screens)). "Run (internal disks read-only)" shows them, read-only. USB drives and SD cards are mounted as usual.
- **Installed on an internal disk,** Hades does not see the PC's other NVMe and SATA disks either, and does not mount the other partitions of its own disk.
- **To show the internal disks by default,** turn on *Settings > LibreELEC > System > Internal disks > Show the internal disks (read-only)*. It is off, and it explains what it means before it is turned on: shown disks can be read by everything that runs in Kodi, add-ons included; "read-only" is how they are mounted, not a lock; and that entry starts the kernel without lock-down. Choosing "Run" once in the boot menu, a reset to defaults or an update with the installer turns it off again.
- **To show only some of them,** *Internal disks that are shown* keeps the NVMe and SATA disks that you untick (the one of Windows, a disk of personal files) away from the kernel. Disks are told by their model and serial number; one that cannot be told apart is kept away too while any is, and so is every disk if the list cannot be read. *Internal partitions that are shown* keeps the partitions that you untick from being mounted. A partition that is not mounted is still on its disk, where a program with full rights could read it.
- **Kernel lock-down.** The entries that hide the disks start the kernel in "integrity" lock-down, and a start that keeps internal disks away locks it down before the system starts: it loads only the modules signed for that build.
- **Devices keep to their own memory.** "Run" and "Live" start the kernel with DMA translation: where the PC has an IOMMU, a network card, a USB controller or the graphics card can use only the memory that the kernel gives it, so a device whose firmware was replaced cannot read the rest. On Intel, the integrated graphics are left out, as Debian does. "Run (internal disks read-only)" starts devices as LibreELEC does, and is the way back if a device misbehaves.
- **The installer entry** of the boot menu is there, but Hades starts in "Run".

### Network and logins

- **Samba (file sharing) is off.** When you turn Samba on, in the first-run wizard or in *Settings > LibreELEC > Services*, a window shows the address, the user (`libreelec`) and a password that Hades generates, and the same page keeps them. Sharing without a password can still be chosen. This is the login that you need to copy an update file ([Updating](#updating)).
- **SSH is off,** as in LibreELEC, and while the root password is the default one it accepts only keys. Set a password first if you want to log in with one.
- **The firewall** starts in "home" mode on a new installation, and **AirPlay** is off.
- **Bluetooth** uses stricter pairing rules, with an option for older input devices.
- **Blu-ray Java (BD-J) menus** run in a service of their own, apart from Kodi, with no network. *Player > Discs > BD-Live: Internet for disc menus* gives them the Internet, and only the Internet ([how](docs/how-it-works.md#bd-live)).

### Updates and privacy

- **Updates.** Hades does not contact LibreELEC's update server, because an official LibreELEC update would remove Dolby Vision. You update by hand ([Updating](#updating)). Update files that are not Hades are refused.
- **No usage statistics** are sent by default.

### The rest

- **Clock.** The PC's hardware clock is kept in local time and never written, so Windows keeps the right time. On a PC without Windows, or whose Windows keeps the clock in UTC, *Settings > LibreELEC > System > OS Configuration > Hardware clock* switches it to UTC from the next start.
- **Hardening.** Kernel and sysctl settings that make attacks harder (a file in `/storage/.config/sysctl.d` can change the sysctl ones back), BusyBox not setuid root, and the add-ons' data, network passphrases and other secrets readable by root only. 32-bit programs are off (`ia32_emulation=1` on the kernel line turns them on).
- **SSD trim.** Once a week, at the lowest priority, the unused blocks of the storage partition are given back to the drive. `systemctl mask fstrim.timer` turns it off.
- **RGB range on AMD.** AMD graphics send limited-range RGB to a TV that cannot select the range, as the TV expects. If you turned on *System > Display > Use limited colour range (16-235)* for such a TV, turn it off.
- **Tone mapping.** On a display without HDR, Kodi's tone mapping method defaults to "Reference (BT.2390)". A saved choice of Hable, Kodi's own default, is switched to it once.

## What has been tested

I test on an AMD Ryzen 9 7950X3D with an NVIDIA GeForce RTX 4080 SUPER, using both the card and the processor's integrated graphics, with an LG C5 TV, a Samsung Odyssey OLED G6 monitor (LS27DG600SLXPE), a TCL Q85H Pro sound bar (on the TV's eARC, or between the PC and the TV) and a HIKSEMI HS-USB-SYNC C PRO 128 GB USB SSD; and on a laptop with an Intel Core i5-12450H. Results marked "author" are mine. Results marked "user" were reported by people on their own hardware, in the issues of this repository and on the LibreELEC and Kodi forums. Seen working means correct colours and a clean return to the menus; the TV's Dolby Vision logo alone does not count. The results below were seen on versions up to 1.0 RC8; what came in 1.0 is under [Not tried on hardware yet](#not-tried-on-hardware-yet).

### Graphics

| Graphics | Tried on |
| --- | --- |
| NVIDIA GeForce RTX 50 series | RTX 5060 (author), RTX 5080 and RTX 5090 (users) |
| NVIDIA GeForce RTX 40 series | RTX 4080 SUPER (author), RTX 4080 Laptop GPU (user) |
| NVIDIA GeForce RTX 30 series | RTX 3050, RTX 3070 and RTX 3090 (users) |
| NVIDIA GeForce GTX 16 and RTX 20 series | GTX 1650 (user, issue #22) |
| NVIDIA GeForce GTX 900 and GTX 10 series, TITAN V (driver 580) | GTX 1060 (user) |
| AMD Radeon RX 7000 series | RX 7600 (user) |
| AMD Radeon RX 5000, RX 6000 and RX 9000 series | No reports yet |
| AMD Radeon RX 400 and 500 series, RX Vega M, RX Vega 56 and 64, Radeon VII (new in 1.0 RC5) | No reports yet |
| AMD Ryzen integrated graphics | Ryzen 9 7950X3D (author), Ryzen 7 5800H, Ryzen 5 5600U and Ryzen 3200G (users) |
| Intel integrated graphics | Core i5-12450H (author), Core i3-1005G1, Core 5 255H and N100 (users) |
| Intel HD and UHD 6xx graphics | HD Graphics 610, with a Pentium G4560 (user) |
| Intel graphics that use the `xe` driver (Arc B-series, Core Ultra 200V and newer) | No reports yet |

TVs and monitors: an LG C5 TV and a Samsung Odyssey OLED G6 monitor (author); LG G5, LG C2, a Panasonic TV and a Sony Bravia 8 II with native Dolby Vision (its HDMI input set to "Enhanced format"), a Samsung S90H, a Samsung S95B and a JVC DLA-NZ9 projector with HDR10+, an LG C3 with Dolby Atmos passed through, an LG C1 and an LG G2 with native Dolby Vision through an AV receiver, a Titan Noir Max projector with native Dolby Vision and with Dolby Vision shown as HDR10 and HDR10+ through an AV receiver, an LG CX through a Yamaha RX-V6A receiver, an LG OLED B7 that gets its picture back after it is switched off and on, and a Sony TV that offers Dolby Vision only as low-latency (users).

### Features

| What | Seen working on |
| --- | --- |
| Dolby Vision profile 7 with FEL | RTX 4080 SUPER, Ryzen 9 7950X3D and Core i5-12450H (author); RTX 4080 Laptop GPU, RTX 5080, RX 7600, Ryzen 7 5800H, Core 5 255H and HD Graphics 610 (users; on the HD Graphics 610, measured: the enhancement layer in every frame, at the last level of the fidelity step-down, with no menus over the picture, issue #12) |
| Dolby Vision profile 7 with MEL | Ryzen 9 7950X3D (author); RX 7600 (user) |
| Dolby Vision profiles 8.1 and 5 | RTX 4080 SUPER and Ryzen 9 7950X3D (author); RX 7600 (user); profile 8.1 on an RTX 5090 (user, issue #16) |
| Dolby Vision in two video tracks | `.m2ts` on the RTX 4080 SUPER (author); MKV on an RTX 3070, and MP4 on a laptop with RTX graphics (users) |
| Menus and subtitles over Dolby Vision at full frame rate | Profiles 7 FEL and 8.1 on the RTX 4080 SUPER and the Ryzen 9 7950X3D (author); profile 7 FEL on an RX 7600 (user, measured with the menus over the picture: 240 of 240 frames, the largest gap 42.0 ms, issue #3) |
| Dolby Vision Low-Latency | Ryzen 9 7950X3D, through an HDMI sync box that offers only low-latency Dolby Vision (author) |
| Dolby Vision through a sound bar or an AV receiver | RTX 4080 SUPER through the TCL Q85H Pro sound bar to the LG C5 (author); Ryzen 5 5600U through a Denon AVC-X3900H to an LG C1 (user, issue #15); HD Graphics 610 through an Onkyo receiver to an LG G2 (user, issue #12); RTX 5090 through a Denon X4800H to a Titan Noir Max projector (user, issue #16); GTX 1060 with driver 580 through a Denon receiver, at every refresh rate (user) |
| Dolby Vision shown as HDR10 | Profile 5 on the RTX 4080 SUPER (author); profiles 5 and 7 FEL on a Core i3-1005G1 (user); an RTX 5090 with a Titan Noir Max projector (user, issue #16) |
| Dolby Vision shown as HDR10+ | Profile 7 FEL on the RTX 4080 SUPER with a Samsung Odyssey OLED G6 (author); profile 7 FEL on an RX 7600 with a JVC DLA-NZ9, shown as HDR10+ with its FEL at full resolution, and on 1.0 RC6, measured: the JVC reports HDR10+ throughout, and DistributionY99, read from the output, follows the peak that the Dolby Vision metadata give each scene (user, issue #10); a Core i3-1005G1 with a Samsung S90H (user, issue #2); an RTX 5090 with a Titan Noir Max projector (user, issue #16) |
| HDR10+ | RTX 4080 SUPER with a Samsung Odyssey OLED G6 (author); Core i3-1005G1 with a Samsung S90H, Ryzen 3200G with a Samsung S95B, Ryzen 7 5800H, and RX 7600 with a JVC DLA-NZ9 (users; on the RX 7600, measured: the HDR10+ packets of four HDR10+ films byte for byte those of a licensed player in 10 of 10 samples, issue #10) |
| HDR10+ as Dolby Vision | RTX 4080 SUPER (author); Ryzen 7 5800H (user) |
| A TV switched off and on gets its picture back | RTX 3050 to an LG OLED B7 at 3840x2160 at 50 Hz (user, issue #11) |
| HDR10 with more than 8 bits on NVIDIA | RTX 4080 SUPER (author); GTX 1060 with driver 580, in YCbCr 4:2:2 at 10 bits, with an LG OLED65C8, straight and through a Denon receiver (user, issue #17) |
| A 24 fps video at exactly 24 Hz on NVIDIA | RTX 4080 SUPER (author) |
| Dolby TrueHD with Atmos, passed through | RTX 4080 SUPER, Ryzen 9 7950X3D and Core i5-12450H (author); RTX 3090 (user) |
| Dolby Digital Plus with Atmos and DTS-HD Master Audio, passed through | RTX 4080 SUPER (author) |
| 3D: both views decoded, the display switched to 3D | Ryzen 7 5800H, with a display that was not named (user) |
| The use and temperature of the graphics in the player information | A laptop with RTX graphics (user) |
| Kodi on the TV in a PC with two graphics chips | My PC, with the TV on the NVIDIA card |
| Started from USB with the internal disks hidden | My PC |
| Windows installer | "Update" on a real USB drive (author); every other operation only on a virtual disk |

Every change is tested before a release: on the host, with the code that the image runs, and on the hardware above where I have it. Reports with the diagnostic switch on ([diagnostics](docs/diagnostics.md)) confirm what is new on more hardware.

### Not tried on hardware yet

These are new in 1.0. Each was checked on the host, with the code that the image runs, or in a virtual machine, but nobody has seen it on a real screen or with real hardware. If you see one work, or fail, please say so in the issues.

- **Check this setup** and **the diagnostic report**, with the diagnostic switch in the settings: tried in a virtual machine, not on a PC with a TV and a receiver.
- **The player information page** (key O, or the red key of a TV remote over HDMI-CEC): how it reads over bright HDR pictures, with the TV in game mode and with Dolby Atmos, and that opening it costs no dropped frames on graphics cards and on integrated graphics.
- **Dolby Vision sent as composed** (the new default): measured against the films, not yet seen on a TV.
- **Dolby Vision on the newest Ryzen processors** (DCN 4.2 and 4.2B), with the read back of the display pipe that gives way to HDR10 in the same video; the setting that turns Dolby Vision on for the Radeon RX 400, RX 500 and Vega cards; and the button *Check the Dolby Vision output*.
- **Dolby Vision, HDR10 and 3D through a passive DisplayPort adaptor** (DP++): not tried with an adaptor.
- **The TV's Dolby Vision block kept whole** where its EDID counts extra blocks with an HDMI Forum EEODB, and the player information that tells a TV input without Dolby Vision from an EDID read in part (issue #7): checked with NVIDIA's EDID code and the kernel's tests, not with that TV.
- **HDR in 4:2:2 at 12 bits over HDMI 2.0** on Intel graphics and on AMD graphics such as those of the Ryzen 7000 series: the picture, the extra change of mode at the start of a video, and whether small graphics keep up. On AMD this reaches a screen for the first time with this version.
- **HDR at more than 8 bits on NVIDIA, now on by default:** YCbCr 4:2:2 at 12 bits on the GeForce GTX 16, RTX 20 and newer cards, and the step back to 8 bits where a TV does not lock onto it. At 10 bits on a GeForce GTX 1060 it was seen working.
- **HDR over HDMI on Intel HD and UHD 6xx graphics,** an expert setting: nobody has tried it on a TV.
- **Tone mapping:** the knee of ITU-R BT.2390 on displays without HDR and in the mappings to the display, HDR10+ adapted by its own metadata and studio tone curve, HDR videos played as SDR, the SDR displays (Bright room, Projector, Custom) and the black of the display, the trims of Dolby Vision as an expert choice, and the peak of the TV set by hand for the automatic tone mapping (v4.0) choices. Measured with the software bridge.
- **Picture in step with its sound,** the pause that keeps passthrough sound, TrueHD that comes back sooner after a start or a seek, and the fixes of DTS-HD passthrough: measured on the host, not with a receiver.
- **A lighter interface during Dolby Vision, HDR10 and HLG,** Dolby Vision made from HDR10, HDR10+ and profile 8.1 at about a third less GPU time, and the packing of Dolby Vision with less GPU time while the interface is over the picture: the picture is meant to be byte for byte the same; the saving has not been measured on real graphics.
- **Memory for the decoder:** the pool of video surfaces is sized from the level of the stream. Not tried on a graphics card with 2 GB.
- **Dolby Vision through the DisplayPort-to-HDMI converter** of a mini PC or NUC board. On by default only on the board where it was seen working elsewhere.
- **The output during and after Dolby Vision:** the output given back as it was found, with the colour range and format of that moment; with NVIDIA's driver 620, its own depth and colour format set to automatic during Dolby Vision; HDMI variable refresh rate left off during native Dolby Vision on NVIDIA GeForce GTX 16, RTX 20 and RTX 30 cards; a colour format or bit depth that another program left on the output put back when Hades starts (the case reported after using a Flatpak app in the same session); the two new settings *Colour format* and *Maximum bit depth*.
- **HLG sent as Dolby Vision,** on its own where the output cannot send HLG, and by choice anywhere else.
- **Larger pictures:** a Dolby Vision video of up to 4096x2304 is scaled down to the 3840x2160 output.
- **Dolby Vision that fell back to HDR10:** films whose metadata repeat a pivot (the Transformers sample of the reports) were refused by the picture check and now play. The check was changed against the specification and the sample's own metadata, not on a screen.
- **HDR10 whose mastering display comes in two forms** no longer sets the output up again in the middle of a video; the check of which video formats the graphics can show follows the layout of each picture.
- **NVIDIA driver 620.70.06** in place of 615; HDR of more than 8 bits on the GeForce GTX 900 and GTX 10 cards flipped on the graphics chip; the kernel log of what the EDID holds.
- **Keeping an NVIDIA card at full speed while HD audio plays through it** (off by default): for the laptop whose sound cut out on its charger.
- **Only the display in use lit, with its sound:** the audio output followed by the display it was chosen with instead of its number, the other displays left off, *Keep the display of the audio output on* for a receiver on its own output, and each output matched with its own EDID: checked on the host, not with several displays.
- **A display connected while Hades runs,** taken without a restart on the same graphics: tried in a virtual machine.
- **DVD films:** films stored as interlaced video put back together and shown at their own rate, and soft-telecined MPEG-2 films at 23.976 Hz, each picture shown once, from files and from DVD images and folders.
- **Streaming through a network drop:** the film waits and goes on from where it stopped; a second video asked for while another opens. Tried in a virtual machine with a server that stops.
- **The library's HDR details from playback,** for a `.strm`, a disc or a stream: tried in a virtual machine with a test add-on, not with a Debrid add-on.
- **Blu-ray:** the audio or subtitle stream that you choose, a play item that starts between key pictures, the enhancement layer of Dolby Vision after a jump or a seek, the chapter marks, joins and subtitles around a jump, the audio and subtitles that a disc chooses, the menus of an HDR disc drawn as the disc made them, Java menus that animate, VC-1 at its own rate, and a disc image over the network. No disc has been tried with these.
- **Picture subtitles:** shown with their own frame, the two new settings for their saturation and for subtitles made for SDR or HDR, and the subtitles of profile 5.
- **3D:** side by side at 1080p for a display that does not list it, the black lines between the eyes of frame packing, and the second view after a picture that empties the decoder. Nobody has tried them on a 3D display.
- **Menus that cannot lock you out:** *Dolby Vision for the menus* asks you to confirm and goes back by itself after 15 seconds; monitors on the same graphics card as the TV are no longer left on with a black picture where Hades is not sure which output carries the sound.
- **The boot menu:** a wait of 5 seconds, brought from 10 once by an update, and no second menu after an update. Tried in virtual machines with UEFI and with BIOS, not on a PC.
- **DMA translation in the normal start** (the IOMMU): tried in virtual machines with the IOMMU of Intel and of AMD; its cost on a graphics card is not measured.
- **First-run wizard and Settings:** the window that shows the Samba login, and nameservers of your own with DHCP. Not tried on a PC.
- **HDMI-CEC:** over a DisplayPort connector, on SECO boards (UDOO x86), an amplifier asked only what volume control needs, and the fixes of libCEC; the kernel log of each InfoFrame (Intel) and packet (AMD).
- **Hardware:** infrared remotes that use the decoders now compiled from source (checked on a virtual receiver), USB disks stopped at power off, the deepest sleep of Ryzen PCs that sleep to idle, the fast side of an external USB hub that may sleep.
- **The wait for the NVIDIA driver** before Hades starts: seen in a virtual machine without an NVIDIA card (no wait), not with one.
- **Intel:** the HDMI check after a TV drops its signal for a moment, no longer lost to a commit under way; the fallback for reading the EDID on Panther Lake and Wildcat Lake.
- **A frame that the driver refuses** no longer leaves a half-drawn picture: tested on the host only.
- **Linux 7.3.4** and its NTFS fixes, including the one for the long wait when a folder on an NTFS disk is opened and for the film that stopped after two minutes (issue #21). Not tried on that reporter's disk.
- **The libplacebo update** (the Dolby Vision picture is meant to be byte for byte the same), **Mesa 26.3.0** with the Dolby Vision output on AMD and Intel graphics, and Matroska seeks that start at the earliest key frame.
- **The diagnostics of a start of the Installer entry:** tried in a virtual machine, not on a PC.
- **For add-ons:** `Player.Process(video.sidedata)`, the same label as in CoreELEC, tried on the host only.
- **The Windows installer:** its new look and its steps, tested on a virtual disk (writing, updating and a partition that another program holds), not yet on a real disk with this version.

SDR, HDR10, HLG, audio passthrough, menus and refresh-rate switching use Kodi's own code and behave as in LibreELEC, apart from the additions above.

## Known limits

- **Dolby Vision needs an untouched HDMI signal** from an HDMI port of the graphics, or from a DisplayPort port through a passive adaptor (DP++) that identifies itself as HDMI (a type 1 adaptor carries 1080p, a type 2 one up to 3840x2160 at 30 Hz); on NVIDIA, also from a DisplayPort port that the graphics drive as HDMI, as the HDMI port of some laptops; on Intel boards on the list, also through the DisplayPort-to-HDMI converter of the board. Variable refresh rate, scaling and colour adjustments in the graphics driver are not compatible with it, and an active converter between DisplayPort or USB-C and HDMI does not carry it. On NVIDIA, a passive adaptor that says DVI can still be sent Dolby Vision at 1080p: the driver decides from the TV's EDID, as it does for HDR and sound.
- Native Dolby Vision goes out at 3840x2160, up to 60 Hz. On Intel UHD 6xx graphics, up to 30 Hz and only from an HDMI port of the graphics itself.
- **Radeon RX 400 and 500 (Polaris) and Vega cards:** the Dolby Vision path is off unless you turn on *System > Display > Dolby Vision on this Radeon (experimental)* (Expert level; it appears only on those cards), because an RX 550 with an LG G3 showed a black picture with the Dolby Vision logo. Dolby Vision plays as HDR10 there.
- **HDR in 4:2:2 at 12 bits over HDMI 2.0** (Intel and AMD graphics) needs one more change of mode at the start of an HDR video at 3840x2160 and 50 or 60 Hz: the TV blanks once more than before. If that bothers you, turn the setting off in *System > Display*, and HDR goes out as before.
- On AMD integrated graphics, the first Dolby Vision video after installing a new build may stutter briefly while Hades measures which of two ways of drawing is faster there.
- On integrated graphics, profile 5 with the player information open dropped frames in testing. Later versions made menus over Dolby Vision cost less GPU time.
- **GeForce GTX 900 and GTX 10 cards and the TITAN V** use NVIDIA's driver 580, which Hades chooses at start by the card. A PC with one of them and a newer NVIDIA card at once gets the newer driver, and the older card gets none. Driver 580 offers Kodi no window of more than 8 bits, so Hades draws HDR10 on these cards into 10-bit buffers of its own (new in 1.0 RC7), and `kodi.log` says the depth that the driver then reports. Over HDMI the driver sends RGB at 8 bits at most on these cards; on the GTX 10 cards and the TITAN V, *HDR at more than 8 bits where the card sends RGB at 8 (NVIDIA)* (on by default) sends HDR in YCbCr 4:2:2 at 10 bits instead where the TV takes it. The GTX 900 cards stay at 8 bits over HDMI. DisplayPort is not under that limit.
- **Dolby Vision Low-Latency is mapped by Hades, not by Dolby's display management**, which is not public. The TV is in its Dolby Vision mode and shows Hades' mapping to the brightness and colours its Dolby Vision block gives.
- **HDR10+ goes out only where the graphics driver can send it with each frame,** otherwise as HDR10; [docs/how-it-works.md](docs/how-it-works.md#hdr10) says where. The player information says "HDR10+" while its metadata go out.
- **HDR10 sent as Dolby Vision** carries metadata measured from the picture as it plays, not the studio's: scene cuts are found from the picture, and a few are missed or found where there is none. **HDR10+ sent as Dolby Vision** keeps the brightness of each scene, not the studio's tone curve, target display or windows; only in Low-Latency, where Hades maps the picture itself, is the studio's tone curve applied.
- **BD-Live** reaches the Internet over IPv4 only, and the updates that a disc can fetch over BD-Live are not implemented by libbluray: such a disc is told that the update failed.
- **3D is new.** I have not seen it on a 3D display, and no 3D disc has been tried. If a 3D video shows a black or broken picture, turn off *System > Display > Switch the display to 3D (HDMI)*. More in [docs/how-it-works.md](docs/how-it-works.md#stereoscopic-3d).
- **The USB receiver of a wireless keyboard, mouse or remote** (2.4 GHz) can lose range next to USB 3 ports, cables and hubs, whose links give off noise in that band. A USB 2.0 port fixed it for a user (issue #13).
- No Secure Boot.
- This is an independent implementation written from public specifications. It has not been tested or certified by Dolby or by HDR10+ Technologies, and it may differ from a certified player.

## What is not included

- **No third-party add-ons and no media sources.** Like LibreELEC, the image has Kodi's standard add-ons, the Kodi and LibreELEC add-on repositories, and LibreELEC's empty local folders. What you install is up to you.
- **No Dolby software.** There is no Dolby code, library or licence key in Hades, and no code or specification of HDR10+ Technologies (the layouts of the HDR10+ InfoFrame and of the Dolby vendor InfoFrame come from GPL code; see "Credits").
- **No telemetry.** Hades adds none and, by default, sends no usage statistics and no identifier of your PC. Like LibreELEC and Kodi, it contacts the services they use (add-on repositories, time servers and the like), which the page "Privacy policy" of the system information lists. The diagnostics stay on your drive.

## Building from source

In outline; LibreELEC's own documentation covers the rest.

1. Use a Linux machine (I use Ubuntu 26.04; LibreELEC keeps container recipes in `tools/docker/`), an unprivileged user, a path without spaces and about 75 GB of free disk.
2. Get this repository: `main` is the current version, and the source of a build is the commit that its `BUILD_ID` names.
3. Build: `PROJECT=Generic DEVICE=Generic ARCH=x86_64 make image`
4. The results are in `target/`: an `.img.gz` (for a USB drive), a `.tar` (for updating) and an `.ova` (virtual machine).

Good to know:

- Every source is downloaded from the address in its recipe (`package.mk`) and checked against the SHA-256 written there. No recipe has an empty hash, and prebuilt "reusable" toolchain packages are not used (`USE_REUSABLE="no"`).
- The NVIDIA driver packages (620 and, for the GeForce GTX 900 and GTX 10 cards and the TITAN V, 580) are downloaded from NVIDIA and unpacked without being run. Only their user-space libraries and firmware are used, unchanged; the kernel modules of 620 and the GBM library of their EGL (egl-gbm) are compiled from NVIDIA's open source code. The kernel module of 580 is built as NVIDIA ships it, with its closed core (`nv-kernel.o_binary`), and its modesetting core and `nvidia-drm` are compiled from NVIDIA's open source of the same version. By building, you accept NVIDIA's licence for those files. Driver 580 makes the image about 120 MB larger.
- Everything in the image is compiled from source, except the files that [NOTICE.md](NOTICE.md) names (NVIDIA's closed parts and the BIOS boot files of syslinux, as LibreELEC takes them) and the firmware. Since 1.0 that includes the infrared remote decoders of v4l-utils, compiled with the clang of the build. The test clips of *Check this setup* are made during the build, with x265 and FFmpeg built for the build machine; no encoder goes into the image.
- The dates written into a build come from the time of its commit (`SOURCE_DATE_EPOCH`), so that builds of the same commit can be compared byte for byte.
- A clean build takes 20 to 45 minutes on a 32-thread machine, much longer on a small one. My builds run without network access, from sources fetched and verified beforehand.
- **If you change the tree and share your build, give it your own name** (`BUILDER_NAME=<your name> make image`) and say that it is yours. Without one, the build calls itself "Hades 1.0".
- The Windows installer is a separate small C# program in `tools/hades-installer/`; `build.ps1` builds it with the compiler that Windows already has.

## Documentation

- [Extras of Hades](docs/features.md): what Hades adds to LibreELEC, one by one, in plain words.
- [Settings](docs/settings.md): what each setting changes on screen or in sound, when to touch it, its default and its level.
- [How it works](docs/how-it-works.md): the path of a video to the TV, told plainly first and in technical detail further down; each graphics driver, HDR10+ and 3D.
- [Diagnostics and logs](docs/diagnostics.md): how to get them, and what to look for.
- [Bluetooth](docs/bluetooth.md): why a device disconnected, and a device used in Windows and in Hades on one PC.
- [Changelog](CHANGELOG.md): what changed in each version.

Versions in 1.0: Linux 7.3.4, Kodi 22 (the `Piers` branch after 22.0rc2, up to 169296d6bd84), FFmpeg 9.0.2, Mesa 26.3.0, libplacebo (d9ab3a48a55a), NVIDIA 620.70.06 and, for the GeForce GTX 900 and GTX 10 cards and the TITAN V, NVIDIA 580.196.07, and OpenJDK 27.0.1 for Blu-ray Java menus, on LibreELEC `master` (13.0 development, up to 23b6180439a3). LibreELEC has not released version 13.

## Credits

What this build adds to LibreELEC was written for it, except what is named here, which is the work of others and is used with thanks:

- **CroqueMr's Dolby Vision fork of LibreELEC, and its contributors** ([CroqueMr/libreelec-x86-DV](https://github.com/CroqueMr/libreelec-x86-DV)), where this work began: its Dolby Vision module for Kodi (`dvbridge`), from which this build's Dolby Vision output grew, its Dolby Vision output for Intel graphics in the Linux kernel (the Dolby vendor InfoFrame that its patches added to `drivers/video/hdmi.c`, with the same interface, was written again for this build, under the licence of that file), and its changes to FFmpeg and libplacebo, among them a faster scaler for profile 7. Several features follow ideas from its releases and were written anew here, among them the Java menus with nothing to install and the settings for the colour format and the depth of the link (its Kodi pull requests 30099 and 30192, by smp79, read as data and written again on the property of the Linux kernel), and, in the packing of the Dolby Vision signal, the value that the two pixels of a pair share worked out once through the quad operations of the GPU and a shorter path for black pixels of the interface; the [changelog](CHANGELOG.md) names each one on its item.
- **Dom Cobley (popcornmix)**: the decoding of 3D Blu-ray video (his changes to FFmpeg's H.264 decoder) and how Kodi reads 3D discs.
- **cinemaONE** ([cinema-ONE/LibreELEC.tv](https://github.com/cinema-ONE/LibreELEC.tv)): the changes to AMD's kernel driver for 3D over HDMI, and Kodi pull requests 29463 (opaque menus over HDR video) and 29060 (picture subtitles resized and moved); and the ideas of 3D through a passive DisplayPort adaptor and of side by side at 1080p, written anew here.
- **Adrian Betschart**: the change to AMD's kernel driver that sends HDMI RGB in limited range on CTA modes, which is part of Linux 7.3 and which two patches of this build build on.
- **quietvoid** ([dovi_tool](https://github.com/quietvoid/dovi_tool) and [hdr10plus_tool](https://github.com/quietvoid/hdr10plus_tool), MIT): references for the Dolby Vision and HDR10+ metadata.
- **CoreELEC** and the GPL kernel code of Amlogic that it carries, read as data: the layouts of the HDR10+ InfoFrame and of the Dolby vendor InfoFrames, the Low-Latency signal, and which Dolby Vision displays are sent which InfoFrame; and the format of the `Player.Process(video.sidedata)` label, which add-ons such as TinyPPI read (from its Kodi branch; the code is written here).
- **YBLOD** ([dangerouslaser/libreelec-yblod](https://github.com/dangerouslaser/libreelec-yblod)): ideas for a few features, written anew here, among them Dolby Vision through the DisplayPort-to-HDMI converter of a board, with the first board of its list, and HDR sent as YCbCr 4:2:2 at 12 bits inside the Dolby Vision signal; and **big-fella**, for his HDMI patches to the Intel and AMD kernel drivers in YBLOD, taken with his authorship, and an idea for AMD graphics.
- **pannal (p3i), avdvplus, SamuriHL and matthane**, whose CoreELEC builds and work gave the ideas for several features, written anew here; SamuriHL's changes to libbluray for BD-J menus, and the CEC fixes of pannal's CoreELEC tree (by CoreELEC, pannal and Stuart Marks), are taken as patches with their authors.
- **harment**: the file that keeps chosen outputs on for their sound (`hades-keep-outputs`), taken as his patch, with his authorship.
- **CorentinB, damnms, Pro-me3us, malard and primez**: ideas written anew here, for films rebuilt from interlaced video, nameservers with DHCP, USB disks stopped at power off, and network reads that stall.
- **The Linux kernel developers:** the fixes to the `fs/ntfs` driver, to the Wi-Fi drivers `mt7921` and `mt7925`, for Intel's Wildcat Lake graphics and for AMD's DCN 4.2B display that Hades takes from mainline, and Ville Syrjälä's fixes of Intel's GMBUS for Panther Lake and Wildcat Lake, from Intel's development tree, with their authors and messages.
- **Fixes from other projects, taken as patches with their authors:** among them avahi, expat, OpenSSL, curl, OpenSSH, glibc, Python, iptables, libdrm, FFmpeg, BlueZ, BusyBox, libbluray (Daniel Oberlin's fixes for BD-J menus), libdvdcss (Saifelden Mohamed Ismail's change for CPXM discs), the pull requests to Kodi of 78andyp, olympia, Markus Härer, tanio99 and heit88, the commits of Kodi's `Piers` branch and `master`, and User65k's fix to Kodi's UPnP from CoreELEC's Kodi.
- **Team LibreELEC and Team Kodi**, and the authors of every program in the image. Patches taken from other projects keep their authors in their headers (the sixteen files of CroqueMr's fork that have no description of their own are named in [NOTICE.md](NOTICE.md)), and Rudi Heitbaum's OpenSSL 4 commit from LibreELEC keeps its author in this repository's history.
- **Everyone who tried Hades and said what they saw,** in the issues of this repository and on the LibreELEC and Kodi forums: the rows marked "user" in [What has been tested](#what-has-been-tested) are theirs. Among them roronoazoro-blip, BigRedJim, cinema-ONE, wright96d, sultan917, Iftuga, andrei-er, Levi741i, vinc-lab-creator, harment, bevigilant, Khazar321, pulento, sat4all1-dev, nissel, SunnySnacks, RaphS8 and SeaMonkey82 on GitHub, and testmest, ht2tweak, lfemperor and vertox on the forums.

[NOTICE.md](NOTICE.md) lists them with their licences.

## Licences and source code

- LibreELEC's build system is under the GNU General Public License, version 2; each file says whether "only" or "or later". Each patch has the licence of the program it changes. Kodi is GPL-2.0-or-later, and so is the code that this build adds to it, among it the Dolby Vision module (`tools/dvbridge`), whose parts that came from code outside CroqueMr's GPL-2.0-or-later offer (the metadata serializer, the content mapping v4.0 helper, the transport packing, the CRC of the metadata packets and the check of P010 DMA-BUF descriptors) were written again from ETSI GS CCM 001, ITU-R BT.2020 and BT.2100, the field layouts of dovi_tool (MIT) and the DRM format definitions; and the files that hold the output while Dolby Vision plays (`OutputHandover.cpp` and the headers around it), written from the KMS interface of the Linux kernel. The pages of the player information for other skins are made from those of Estuary and keep its licence, GPL-2.0-only. As LibreELEC builds it, Kodi is linked with the Samba client (GPL-3.0-or-later), FFmpeg built under version 3 of the GPL and OpenSSL (Apache-2.0), so the Kodi built here is distributed under GPL-3.0-or-later.
- The texts of the licences are in `licenses/`, and the licence of each package is in its recipe (`PKG_LICENSE`).
- The NVIDIA user-space libraries and firmware, and the closed core of the kernel module of driver 580 (loaded only for the GeForce GTX 900 and GTX 10 cards and the TITAN V), are proprietary and come under the NVIDIA Driver License Agreement, not under a free licence. Some firmware files for other hardware have their own licences.
- The complete source of a build is this repository at the commit named in `BUILD_ID` in `/etc/os-release` of the running system, plus the source archives that its recipes name. See [NOTICE.md](NOTICE.md) for credits, trademarks and how to obtain the source.
- LibreELEC's own README, with its licence and copyright statement, is kept as [README.LibreELEC.md](README.LibreELEC.md), and its guide to contributing to LibreELEC as [CONTRIBUTING.LibreELEC.md](CONTRIBUTING.LibreELEC.md).

## Reporting problems

Report problems with Hades in the [issues](https://github.com/hadesdevls/LibreELEC-Hades/issues) of this repository, not to LibreELEC, Kodi, Dolby, HDR10+ Technologies, NVIDIA, AMD or Intel. Please include:

- the build (*Settings > System information*, or the `PRETTY_NAME` and `BUILD_ID` lines of `/etc/os-release`);
- the graphics (the card, or the processor for integrated graphics), the TV model and how they are connected;
- what you played (container, Dolby Vision profile or HDR format) and what you saw;
- a diagnostic report: *Settings > LibreELEC > System > Submit Log > Create a diagnostic report* makes one file and can copy it to a USB drive. Look through it first: it is filtered, but it is yours to check. If Kodi does not start at all, turn the diagnostic switch on in the Windows installer and send the `hades-diag` folder of the drive.

[docs/diagnostics.md](docs/diagnostics.md) says how to get them, also from a system installed on an internal disk.

If the same problem happens with an official LibreELEC image, it belongs to LibreELEC or Kodi and can be reported there.

There is no warranty of any kind. You use this at your own risk.
