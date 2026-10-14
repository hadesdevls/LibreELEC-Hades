LIBREELEC HADES  -  DIAGNOSTICS
===============================
(Versión en español: LEEME.txt)

While a file named DIAG-ON.TXT is at the top of this drive (the boot partition of LibreELEC Hades), LibreELEC
Hades leaves in this folder what is needed to find out why something does not work on a PC. It changes no
setting and turns nothing off. To stop it, put OFF back in the name of that file (DIAG-OFF.TXT), or delete
it. (Any name that starts with DIAG-ON counts as on.)

It only reads what the system is doing, with three exceptions, all of them a minute after each start and
all for a TV that is plugged in and not seen:
  - it asks the HDMI and DisplayPort sockets that have nothing connected once more, as the media centre
    does when it starts. Not once a video has been opened;
  - it lists the devices of the PC, which wakes a graphics card that was asleep;
  - where no TV or monitor is seen on any socket, the kernel writes down more about the displays for about
    three minutes, or until a video is opened. Where a TV or monitor is seen, this is not done.

WHAT IS WRITTEN HERE

  Each start of the PC has a folder of its own: start-1, start-2, and so on. The last eight are kept, so a
  drive that is taken from one PC to the next keeps what each of them had to tell. In each of them:

  1-minute, 4-minutes
      About 1 and 4 minutes after the media centre starts: the graphics cards and what is connected to
      each (with the EDID of every display), the kernel log, the last of the media centre's log
      (kodi.log), the warnings of the system, sound devices, USB devices, the state of the network,
      services, the clock. The first of the two also has:
        - redetect.txt: what the kernel said when the HDMI and DisplayPort sockets with nothing connected
          were asked again, and whether a graphics card was asleep. An AMD graphics card answers from what
          it last saw: it looks at a socket when it starts, when it wakes up and when something is plugged
          in or out;
        - lspci.txt: the devices of the PC;
        - acpi-tables.tar.gz: the description of the PC that its firmware gives (how its sockets are wired
          and switched). It is much the same for every PC of that model.
      With some PCs there is also gpio.txt: the state of the lines that the firmware uses for its sockets
      and buttons.

  during-dv-1 to during-dv-6
      About 15 seconds after each Dolby Vision playback starts: what is really sent to the TV.

  after-dv-1 to after-dv-6
      5 and 30 seconds after each Dolby Vision playback ends. Written at once, so it is there even if the
      PC has to be turned off by force (for example, when the TV is left without a picture).

  shutdown
      When the PC is turned off or restarted from the menu on the screen: the logs of that session, what
      was connected to each socket at that moment, and how the drive and the graphics card were doing
      second by second. Always turn the PC off from that menu, not with its button.

  Of the two kinds of Dolby Vision, the first six playbacks of a start are kept.
  Nothing is written when this drive has less than 128 MB free: a note in this folder, NO-ROOM.txt, then
  says so.
  Folders named 1-minute, shutdown-1 and the like beside the start folders are of an earlier version.

PRIVACY

  Before a log of the media centre or of the services of the system is written here, it goes through a
  filter that removes the path of every address (only the name of the server is left), e-mail addresses,
  and what looks like a token, a key or a password. Without that filter no such log is copied. The log of
  the kernel (dmesg, and the part of it in redetect.txt) is written as it is: it names your devices, and
  the address of a network folder if one was connected by the system itself. No filter is perfect: look at
  the files before you give them to anyone.

  What does stay, because it is what tells one PC from another:
    - the names of your add-ons and of what you played, as the media centre logs them;
    - the make, model and serial number of your displays, sound devices, disks, USB devices and graphics
      card, as they give them, the make and model of the PC, and the names of the drives that were plugged
      in;
    - the name of this PC on the network, the name and the addresses of your network (also those of the
      network cards, of the access point and of Bluetooth devices), and which services were listening;
    - your time zone.

  Nothing is sent anywhere: the files stay on this drive.

GOOD TO KNOW

  With some graphics drivers the files drm-state are nearly empty. That is expected.

  If a TV or monitor that is plugged in shows nothing:
    1. Start the PC with it plugged in and turned on, and wait two minutes.
    2. Pull its cable out of the PC, wait ten seconds, and plug it back in.
    3. Wait three more minutes, and turn the PC off from the menu (on the screen that does work).
  The folder of that start then tells what each socket saw: at the start (1-minute), when the cable went
  back in (4-minutes) and at the end (shutdown). Asking a socket again can make a display appear that had
  been missed: redetect.txt then shows it as connected.
