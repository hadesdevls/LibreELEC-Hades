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
  SHA256SUMS, SHA256SUMS.sig     the list of the files of the release and its signature: with them the
                                 installer checks that the image is the one LibreELEC Hades published
  LEEME.txt                      this same text, in Spanish

What you need
  - Windows 10 or 11.
  - A USB stick or a USB disk of 8 GB or more that can be erased. With an SSD on USB it runs much more
    smoothly than with a stick.
  - For Dolby Vision: a TV that supports it and NVIDIA graphics (a GeForce GTX 900 or later), or recent
    AMD or Intel graphics, connected by HDMI straight to the TV.

Installing
  1. Double-click LibreELEC-Hades-Installer.exe. Windows warns you twice, and that is normal:
       - "Windows protected your PC": click "More info" and then "Run anyway". It appears because the
         program does not carry a paid signature.
       - "Do you want to allow this app to make changes to your device?": Yes. It is needed to write to
         the drive.
     The installer is in English, or in Spanish when Windows is in Spanish. The link at the bottom left
     of the dark side bar ("Español" / "English") changes it. It also follows Windows, light or dark.
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
     to check it. Each drive in the list shows its model, size and serial number, to be sure it is the
     right one. At the end, the window tells you how to start Hades and what each entry of its menu does.
  Films on a USB disk or stick: plug it in. Hades opens USB drives by itself, however it was installed.

Starting Hades
  1. Restart the PC and, as soon as it turns on, press the key of the boot menu several times:
       ASUS F8, MSI F11, Gigabyte F12, ASRock F11; laptops: F12 or Esc
  2. Choose the USB drive (the entry that says UEFI) or the disk where you installed it.
  3. The first time it prepares its space and restarts by itself, back into Hades (on a PC that starts in
     the old way, by BIOS, choose it again in the menu).
  Hades shows its own boot menu, which starts the entry Run by itself after 5 seconds (a key stops it, to
  choose another entry); how long it waits is changed in the LibreELEC settings (System, Boot menu: 10, 5,
  3 or 1 second), and updating with this installer keeps it.
  To go back to Windows: shut Hades down and turn the PC on without pressing anything, or choose "Restart
  into Windows" in the power menu of Hades (it appears when the PC has Windows; the next start is
  Windows, whatever comes first in the PC).

Starting Hades from Windows, without the key
  In the installer, the button "Restart into LibreELEC Hades" restarts the PC into Hades once: the next
  time the PC starts, Windows starts as always. It is next to a drive or disk that already has Hades (when
  you choose it), on the last screen after installing or updating, and, as a link, in the dark side bar
  (above the language). It asks first. Next to it, the box "Also put a shortcut for this on the desktop"
  (off unless you tick it) puts there, when the PC restarts, a shortcut that does the same (Windows asks
  for permission every time); ticking it makes nothing by itself. It only works when the PC lists the
  drive with Hades in its boot menu: most PCs list a USB drive that was plugged in when they started; if
  yours does not, the window says so and which key to press. It changes nothing else in the PC: it does
  not add or remove anything in its boot menu, and the order of that menu stays as it is.

Updating to a new version
  1. Put the new image (.img.gz and its .sha256, with SHA256SUMS and SHA256SUMS.sig of its release) in
     this folder, or pick it with "Choose another...".
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
  of the system and of Hades with passwords, keys, network addresses and the names of your files and
  folders replaced by markers, and what is sent to the TV during Dolby Vision. It is off unless you turn
  it on, and it sends nothing anywhere: it all stays on the drive, in a folder named hades-diag.
  1. Turn it on: tick "Save diagnostics there" in the installer, when you install or when you update. On
     a USB drive it can also be done by hand: Windows shows a part of the drive as LIBREELEC, and there
     the file DIAG-OFF.TXT is the switch. Change OFF to ON in its name, so that it is DIAG-ON.TXT. (If
     Windows shows it as DIAG-OFF, without the ending, name it DIAG-ON: the ending stays as it is.)
  2. Start Hades, do what does not work, wait five minutes, and turn the PC off from its menu. If what
     fails is the entry Installer of the boot menu, start that entry with the diagnostics on: it keeps
     its log too, as it goes. If the PC stops while it starts, turn it off, start it once more and wait
     five minutes: the folder of that next start keeps, in previous-boot, how the start that stopped
     ended. While the diagnostics are on, the boot menu also has the system write down each driver as
     it starts it, so that this tells which one it was waiting for (with the box of the installer, from
     the very first start; turned on by hand, from the start after the first one).
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
    and turn the PC off from the menu of Hades. The folder hades-diag then tells what each socket saw.

Secure Boot (optional)
  Without this option Hades needs Secure Boot turned off in the BIOS. If your PC keeps Secure Boot on,
  as many Windows 11 PCs do, tick "Start with Secure Boot" in the installer. The box is off unless you
  tick it, and with it off nothing changes. What it does:
  - The installer makes a key that belongs to this PC alone. It is kept in the Hades storage and nowhere
    else: not in Windows, not in a file of yours. GRUB and the kernel of Hades are signed with it, and
    Hades starts through "shim", the boot program that Debian publishes and Microsoft signs.
  - The first time Hades starts, a blue screen appears (it is MokManager, and it is normal). Choose
    "Enroll MOK", "Continue", "Yes", type the 4-digit code that the installer shows when it is done, and
    choose "Reboot". It is done once; after that Hades starts with no questions. If this PC did not take
    the request, the installer says so: then choose "Enroll key from disk", the disk of Hades and the file
    HADES-SB.CER (in the part of Hades that Windows calls LIBREELEC), and no code is asked for.
  - BitLocker: accepting the key does not ask for the recovery key. Turning Secure Boot itself on or off
    in the BIOS can, so keep the recovery key at hand if you change it.
  - Another Linux on this PC that starts with Secure Boot should be up to date (February 2025 or later):
    the boot program of Hades, from Debian 13, keeps older Linux boot loaders from starting (a list in
    the firmware, SbatLevel). Windows is not affected.
  - The BIOS has to trust the Microsoft certificate that Linux boot programs use ("Microsoft 3rd Party
    UEFI CA", or "Allow Microsoft 3rd Party UEFI CA" in some BIOS); the installer tells you if this PC's
    does not have it.
  - Updating: the box starts ticked for a drive whose Hades has its key. The update signs again with the
    key that Hades keeps in its storage, and there is nothing to accept again. If that key is not there
    any more, or the storage has not started with this version yet, the installer says what to do (it
    offers a new key, which has to be accepted again on the blue screen). Updating with the box unticked
    puts the plain boot back: then Secure Boot has to be off in the BIOS again.
  - Going back: untick the box when you update. The key accepted on the blue screen can stay: removing
    Hades deletes its storage, and with it the private key, and the certificate that stays in the firmware
    cannot sign anything alone. (To delete it too: from another Linux with Secure Boot, "mokutil --delete"
    with the file HADES-SB.CER, then "Delete MOK" on the blue screen of the next start.)
  - The installer never changes the keys of Secure Boot (PK, KEK, db, dbx) or the boot entries of the PC.
    The only things it can write in the firmware are the three variables with which shim is asked to offer
    the key at the next start (MokNew, MokAuth and MokTimeout), and only when this box is ticked.

Removing it
  - From a USB drive: open the installer, choose "On a USB drive", the drive, and "Remove LibreELEC Hades
    from this drive". It is left as a normal USB drive, empty, in exFAT (Windows, Mac and most TVs read
    it). If Windows says that a part of the drive has to be formatted when you plug it in, that is the part
    of Hades, which Windows cannot read: do not format it if you want to keep Hades.
  - From an internal disk: open the installer, choose "internal disk", the disk, and "Remove LibreELEC
    Hades". If it shared the disk with your files, the partition gets its space back and your files are
    not touched.

What the installer never does
  - It does not offer the disk that Windows starts from.
  - It does not erase the disk where this program, the image or your user files are: on that disk it can
    make room ("Make room without erasing anything") and later update or remove Hades, but never "Use the
    whole disk".
  - It does not touch the boot of Windows or its boot partition.
  - It does not change the keys of Secure Boot or the boot entries of the PC (only, with the Secure Boot box
    ticked, the three variables that ask shim to offer the key of Hades, and it can delete them again).
  - It sends nothing anywhere: it does not use the network.
  What it does is written to LibreELEC-Hades-Installer.log, next to the program. That file names the
  disks of your PC: delete it before you pass this folder on to someone else.

The tests of the installer
  LibreELEC-Hades-Installer.exe is all you need. Its self-tests are in a second program for developers,
  built from the same source code (the folder tools/hades-installer of the project).

Licences
  This installer is free software under the GNU General Public License, version 2 or (at your option)
  any later version: COPYING.txt, next to it, is the text of that licence. It comes with no warranty.
  LibreELEC Hades is LibreELEC (https://libreelec.tv) with changes, under the GPL-2.0 licence. Its media
  centre is Kodi (https://kodi.tv), modified: that is why it shows itself as LibreELEC Hades and not as
  Kodi, which is a registered trademark of the XBMC Foundation. It includes other free software as well,
  and the NVIDIA drivers under their own licence. It is not made or endorsed by LibreELEC, by Team Kodi or
  the XBMC Foundation, by Dolby, or by NVIDIA, AMD or Intel.
  The source code of LibreELEC Hades and of this installer (the folder tools/hades-installer) is in the
  repository of the project, which is where this folder is published. The image names the exact source
  it was built from (BUILD_ID in /etc/os-release) and carries the licence texts: the folder "licenses"
  and the file NOTICE.md on the part of the drive that Windows shows as LIBREELEC.
