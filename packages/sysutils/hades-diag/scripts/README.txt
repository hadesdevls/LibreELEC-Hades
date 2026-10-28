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

  Each start of the PC has a folder of its own: start-1, start-2, and so on. The number goes on from the start
  before, also after an update of the system or once the folders have been deleted, so a name is never given
  to two starts. The last eight are kept, so a drive that is taken from one PC to the next keeps what each of
  them had to tell. In each of them:

  start.txt
      Which start the folder is of: its number, the build of LibreELEC Hades, and when the PC started.
      What is in a folder is always of that one start: a folder of the same name that is of another start
      (for example, left by an earlier version) is emptied before this start writes there, and start.txt
      then says what it was.

  1-minute, 4-minutes
      About 1 and 4 minutes after the media centre starts: the graphics cards and what is connected to
      each (with the EDID of every display, without its serial number), the kernel log, the last of the
      media centre's log (kodi.log), the warnings of the system, sound devices, USB devices, the state of
      the network, services, the clock, and Bluetooth (bluetooth.txt: the controllers and their firmware,
      the settings, and of each paired device its kind, its state and the connection that it asked for;
      never a key, and its name only as a marker, below). The first of the two also has:
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
      About 15 seconds after each Dolby Vision playback starts: what is really sent to the TV. With an
      NVIDIA graphics card, also nvidia-smi.txt: its clocks, what holds them down, its power and its
      temperature at that moment (state.txt says when; the rest is taken 12 seconds later).

  after-dv-1 to after-dv-6
      5 and 30 seconds after each Dolby Vision playback ends. Written at once, so it is there even if the
      PC has to be turned off by force (for example, when the TV is left without a picture).

  shutdown
      When the PC is turned off or restarted from the menu on the screen: the logs of that session, what
      was connected to each socket at that moment, and how the drive and the graphics card were doing
      second by second. Always turn the PC off from that menu, not with its button.

  Each of these folders also has build.txt: the build of LibreELEC Hades that wrote it.
  Of the two kinds of Dolby Vision, the first six playbacks of a start are kept.
  Nothing is written when this drive has less than 128 MB free: a note in this folder, NO-ROOM.txt, then
  says so.
  Folders named 1-minute, shutdown-1 and the like beside the start folders are of an earlier version.
  Folders that a version before this one wrote without the filter below, or put through it before it
  took the names of Bluetooth devices out, are put through it once, when this version starts; each folder
  that has been through it has a file named .filtered, in which this version writes 2.

PRIVACY

  Everything that is written here goes through a filter first. Without the filter nothing is copied.
    - Addresses of your network and of the internet (IPv4 and IPv6), MAC and Bluetooth addresses, the
      names of Wi-Fi networks, the name of this PC on the network and those of the computers of your home
      network in addresses, the serial numbers of displays, graphics cards and USB devices (also inside
      the EDID of a display), the UUIDs of disks and partitions, SSH keys and their fingerprints, and the
      names of the Bluetooth devices that this PC knows (those that are paired, and those that it saw when
      it looked for devices: a keyboard or a phone is often named after its owner, as in "Magic Keyboard
      de Juan") become markers, such as [lan-ip-1a2b.3c4d], [mac-5e6f70] or [bt-name-7a8b9c]. A marker is
      the same for the same value on this PC, so the files still tell which lines are about the same
      device or the same network, but it cannot be turned back into the value: it is made with a secret
      that never leaves this PC.
    - From the logs of the media centre and of the system, the path of every address (only the name of
      the server stays), the names of films, songs and searches (only the extension stays, as in
      [redacted].mkv), e-mail addresses, and what looks like a token, a key or a password are removed.
  No filter is perfect: look at the files before you give them to anyone.

  What does stay, because it is what tells one PC from another:
    - the names of your add-ons, and the names of your drives as you labelled them;
    - the make and model of your displays, sound devices, disks, USB devices, graphics card and PC;
    - the names of public servers (for example, of the services that your add-ons use), and which
      services of this PC were listening;
    - your time zone.
  And a name of a Bluetooth device that is short or one plain word, such as "TV", "K" or "Keyboard",
  stays where it stands as an ordinary word: it becomes a marker only where a line names an input device
  by it.

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
