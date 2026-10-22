LIBREELEC HADES: how to install it
==================================
(Versión en español: LEEME.txt)

LibreELEC Hades is a system for watching films on your PC with real Dolby Vision over HDMI. You start it
instead of Windows when you want to watch something, and when you restart you are back in Windows. It
changes nothing in Windows.

In this folder
  LibreELEC-Hades-Installer.exe  the installer: double-click it
  LibreELEC-...img.gz            the Hades image (there is no need to unpack it)
  LibreELEC-...img.gz.sha256     its checksum: with it the installer checks that the image arrived complete
  LEEME.txt                      this same text, in Spanish

What you need
  - Windows 10 or 11.
  - A USB stick or a USB disk of 8 GB or more that can be erased. With an SSD on USB it runs much more
    smoothly than with a stick.
  - For Dolby Vision: a TV that supports it and an NVIDIA RTX 20 or later graphics card (or a GTX 16), or
    a recent AMD or Intel one, connected by HDMI straight to the TV.

Installing
  1. Double-click LibreELEC-Hades-Installer.exe. Windows warns you twice, and that is normal:
       - "Windows protected your PC": click "More info" and then "Run anyway". It appears because the
         program does not carry a paid signature.
       - "Do you want to allow this app to make changes to your device?": Yes. It is needed to write to
         the drive.
     The installer is in English, or in Spanish when Windows is in Spanish. The link at the top right
     ("Español" / "English") changes it.
  2. Choose where you want it:
       - On a USB drive (recommended). The whole drive is erased. No disk of the PC is touched, and from
         there Hades does not even see the disks of the PC.
       - On a secondary internal disk (never the Windows one). The program explains the two ways: making
         room without erasing anything, or using the whole disk. Installed there, Hades does not see the
         other NVMe and SATA disks of the PC either, and it does not open the partitions that it shares
         its disk with. The entry "Run (internal disks read-only)" of its start menu shows them all,
         without being able to change them. A switch in the settings (LibreELEC, System, Internal
         disks) makes that entry the one that starts by itself: it is off until you turn it on, it
         explains what it means first, and updating with this installer turns it off again.
  3. The program asks you to type a word before it touches anything, writes, and reads back what it wrote
     to check it.
  Films on a USB disk or stick: plug it in. Hades opens USB drives by itself, however it was installed.

Starting Hades
  1. Restart the PC and, as soon as it turns on, press the key of the boot menu several times:
       ASUS F8, MSI F11, Gigabyte F12, ASRock F11; laptops: F12 or Esc
  2. Choose the USB drive (the entry that says UEFI) or the disk where you installed it.
  3. The first time it prepares its space and restarts by itself: choose it again in the menu.
  To go back to Windows: shut Hades down and turn the PC on without pressing anything.

Updating to a new version
  1. Put the new image (.img.gz and its .sha256) in this folder, or pick it with "Choose another...".
  2. Open the installer and choose the drive or the disk that already has Hades (it says so under its
     name: "has LibreELEC Hades"). The green button "Update LibreELEC Hades" appears.
  3. Updating replaces only the system. Your settings, your add-ons and whatever you have saved stay as
     they were. "Install from scratch", on the other hand, erases everything.
  If a program has the Hades partition of that drive open (Windows calls it LIBREELEC), nothing is
  written and the installer asks what to do: close the program (a File Explorer window that shows it,
  for example) and press Update again, or let the installer take that partition from the program. WSL,
  the Linux of Windows, keeps open the disk drives that were connected when it started, without showing
  it anywhere: close WSL first, or finish what you are doing there and let the installer take the
  partition. Do not let it take the partition while you are copying something from it.

If something does not work: diagnostics
  Hades can keep what is needed to find out why: which graphics cards and displays the PC has, the logs
  of the system and of Hades with every password, key and address path removed, and what is sent to the TV
  during Dolby Vision. It is off unless you turn it on, and it sends nothing anywhere: it all stays on the
  drive, in a folder named hades-diag.
  1. Turn it on: tick "Save diagnostics there" in the installer, when you install or when you update. On
     a USB drive it can also be done by hand: Windows shows a part of the drive as LIBREELEC, and there
     the file DIAG-OFF.TXT is the switch. Change OFF to ON in its name, so that it is DIAG-ON.TXT. (If
     Windows shows it as DIAG-OFF, without the ending, name it DIAG-ON: the ending stays as it is.)
  2. Start Hades, do what does not work, wait five minutes, and turn the PC off from its menu.
  3. Back in Windows, a USB drive has the folder hades-diag on LIBREELEC. With Hades on an internal disk,
     Windows does not show that part. Get the folder onto a USB stick (one that Windows reads: FAT32,
     exFAT or NTFS) in one of two ways:
       - in Hades: LibreELEC settings, System, "Copy the diagnostics to a USB drive", and choose the stick;
       - with nothing on the screen (when the TV stays dark): make a folder named hades-diag at the top of
         the stick in Windows, plug the stick into the PC while Hades is running, wait a minute, and pull
         it out. Hades copies its folder into that one, in a folder named after the date and time.
     Either way the copy is checked file by file, and nothing is overwritten on the stick.
  Each start of the PC has its own folder in there (start-1, start-2, and so on; the last eight are
  kept), so one drive can be taken to several PCs. A note in that folder (README.txt) says what each part
  is and what of it is personal. Read it before you give the folder to anyone.
  To turn it off: put OFF back in the name of the file, or update with the box unticked (for a drive
  that has the diagnostics on, the installer shows the box ticked, so that an update keeps them). Updating
  replaces the part of the drive where the folder is, and the installer keeps the folder: it copies it
  into the new part, checks it file by file, and says at the end what it kept (and what not, if anything
  could not be kept).

If the TV stays dark (a laptop, or a PC with two graphics cards)
  Hades shows itself on one screen: the TV on HDMI when it sees one, else a monitor, else the screen of a
  laptop itself. Connect the TV and turn it on before you start Hades.
  - If Hades comes up on the laptop's own screen, a note there says that it sees no TV, and on which
    sockets. A TV that is connected later is noticed: Hades asks whether to move to it, and restarts
    its menus to do so (with no answer in 20 seconds it moves by itself, unless something is playing).
  - Which screen is used can be fixed in its settings: Settings, System, Display, Monitor. Choosing the
    laptop's own screen there also turns that note off.
  - If Hades never sees the TV: turn the diagnostics on (above), start with the TV connected and turned
    on, wait two minutes, pull the TV's cable out of the PC and plug it back in, wait three more minutes,
    and turn the PC off from Hades's menu. The folder hades-diag then tells what each socket saw.

Secure Boot
  Hades does not start with Secure Boot turned on yet. If your PC has it on (the installer tells you), it
  has to be turned off in the BIOS to use Hades, and turned back on for the games that require it.

Removing it
  - From a USB drive: format it in Windows (right-click it, Format). If Windows says that a part of the
    drive has to be formatted when you plug it in, that is the part of Hades, which Windows cannot read.
  - From an internal disk: open the installer, choose "internal disk", the disk, and "Remove LibreELEC
    Hades". If it shared the disk with your files, the partition gets its space back and your files are
    not touched.

What the installer never does
  - It does not offer the disk that Windows starts from.
  - It does not erase the disk where this program, the image or your user files are: on that disk it can
    make room ("Make room without erasing anything") and later update or remove Hades, but never "Use the
    whole disk".
  - It does not touch the boot of Windows or its boot partition.
  - It sends nothing anywhere: it does not use the network.
  What it does is written to LibreELEC-Hades-Installer.log, next to the program. That file names the
  disks of your PC: delete it before you pass this folder on to someone else.

Licences
  LibreELEC Hades is LibreELEC (https://libreelec.tv) with changes, under the GPL-2.0 licence. Its media
  centre is Kodi (https://kodi.tv), modified: that is why it shows itself as LibreELEC Hades and not as
  Kodi, which is a registered trademark of the XBMC Foundation. It includes other free software as well,
  and the NVIDIA drivers under their own licence. It is not made or endorsed by LibreELEC, by Team Kodi or
  the XBMC Foundation, by Dolby, or by NVIDIA, AMD or Intel.
  The source code of LibreELEC Hades and of this installer (the folder tools/hades-installer) is in the
  repository of the project, which is where this folder is published. The image names the exact source
  it was built from (BUILD_ID in /etc/os-release) and carries the licence texts: the folder "licenses"
  and the file NOTICE.md on the part of the drive that Windows shows as LIBREELEC.
