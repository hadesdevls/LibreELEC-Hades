# Extras of Hades

What LibreELEC Hades adds to LibreELEC, one by one, in plain words. Each entry says what it does, when you would want it, where to control it and how far it has been tried. For the exact behaviour of a setting, see the [settings guide](settings.md). For how a video travels to the TV, see [how it works](how-it-works.md).

Nothing here replaces anything that LibreELEC does. Without a Dolby Vision TV, most of this does nothing at all, and a video plays as it does in LibreELEC.

## Words used in these pages

- **HDR10, HDR10+, HLG.** Ways to carry a brighter picture with richer colours than ordinary video (SDR). HDR10 sends one description for the whole film. HDR10+ sends a new description for each scene. HLG is the form that broadcasters use.
- **Dolby Vision.** Another form of HDR. Its description changes frame by frame, and the TV uses it to show each scene as the film's colourist intended.
- **Profile 5, 7, 8.1.** The kinds of Dolby Vision file. Profile 5 is what streaming services use. Profile 7 is what UHD Blu-ray discs use, in two layers. Profile 8.1 is single-layer Dolby Vision with an HDR10 picture underneath, common in files.
- **FEL and MEL.** Profile 7 has a base layer and an enhancement layer. The full enhancement layer (FEL) adds real picture detail to the base layer. The minimal one (MEL) adds none. Hades rebuilds the FEL picture in the PC, because the TV cannot.
- **Standard, or TV-led.** The normal way a Dolby Vision player works: the player sends the picture and the metadata, and the TV decides how to show it. **Low-Latency, or player-led,** is a second form for game modes, in which the player does that work.
- **Trims.** The colourist's fine adjustments for how each scene should look on displays of a few given brightnesses.
- **Tone mapping.** Squeezing a picture that is brighter than your display can show into what it can show.
- **Passthrough.** Sending the sound to the receiver exactly as it is on the disc, so that the receiver decodes it (Dolby Atmos, DTS-HD).
- **EDID.** The description that a TV or monitor gives the PC of what it can show.
- **Settings level.** Kodi hides the settings that few people need. The level (Standard, Advanced, Expert) is set at the bottom of the left-hand list of a settings screen. Hades puts what most people want at Advanced and the rest at Expert.

## Dolby Vision

### Native Dolby Vision over HDMI

**What it does.** Sends a Dolby Vision video to the TV as Dolby Vision, in the TV-led mode, with the metadata of every frame. For profile 7 the PC combines the base and enhancement layers, so the full picture of a UHD Blu-ray reaches the TV.

**You would want it** whenever you have a Dolby Vision TV: it is on by default.

**Where.** *Settings > System > Display > Native Dolby Vision output*. In the player, *Dolby Vision output (this video)* chooses for one video.

**Seen working** on NVIDIA, AMD and Intel graphics ([what has been tested](../README.md#what-has-been-tested)).

**Safe by design.** Hades uses it only when the video, the TV, the HDMI link and the graphics driver all qualify. If anything is in doubt, or it fails during a video, the video goes on as HDR10 or SDR, and the player information says why.

### The player information page

**What it does.** Opens a panel over the video. Its first line is a sentence: either that the video plays as intended, or what is wrong and what to do. Below it, three parts say what the file carries, what Hades does with it, and what reaches your TV. More pages hold the details, among them the Dolby Vision metadata and the use and temperature of the graphics.

**You would want it** to check that Dolby Vision is really on, or to see why it is not.

**Where.** Press O on a keyboard, or the red key of a TV remote that sends keys over HDMI-CEC. There is also a button in the video's on-screen controls.

**Status.** New in 1.0. Not tried on hardware yet ([README](../README.md#not-tried-on-hardware-yet)).

### Check this setup

**What it does.** Plays short test clips that the build of Hades makes itself: a test picture in SDR, HDR10, HLG and Dolby Vision (profiles 8.1 and 5), a soft noise from each speaker in turn, and a chime in each sound format that a receiver or TV may take as it is (Dolby Digital, Dolby Digital Plus, Dolby TrueHD). After each one it says in plain words what went out (the display mode, the HDR signal, whether Dolby Vision went out natively and why not, the sound format), asks what you saw or heard, and says what to do if it was wrong: for example, that pink and green colours mean the TV did not switch to Dolby Vision, and that a receiver in between may be the cause. On the SDR picture it also asks how many of the dark and light squares you can see, which tells whether the black level and the contrast of the TV are right.

**You would want it** after installing, after changing the TV or the receiver, or when a sound format plays silence. It switches on only the sound formats that you heard, and switches one off only if you say so; nothing of the picture settings changes. DTS cannot be checked this way.

**Where.** *Settings > LibreELEC > System > Check this setup*. *Show the last result* shows what it found last time. Tried in a virtual machine; not tried on hardware.

### A notice of the mode when a video starts

**What it does.** A short line appears a moment after a video starts: for example "Dolby Vision, FEL, 3840x2160 at 23.976 Hz".

**Where.** *Settings > System > Display > Show the playback mode when a video starts*. Off by default.

### Dolby Vision for the menus

**What it does.** Keeps the TV in Dolby Vision while you browse, so that a Dolby Vision film starts without the TV changing its picture mode. The menus look as the TV shows Dolby Vision, which can differ from its SDR look.

**You would want it** to avoid the pause and flash when the TV changes mode.

**Where.** *Settings > System > Display > Dolby Vision for the menus*. Off by default. When you turn it on, a question asks you to confirm, and the setting goes back to Off by itself after 15 seconds unless you answer Yes, so a black picture cannot lock you out.

### Dolby Vision Low-Latency (game-mode inputs)

**What it does.** Some TV inputs offer Dolby Vision only in its low-latency form, which is what they use in game mode. Hades can send that form. The PC then maps the picture to your display itself, with its own method and not Dolby's, which is not public.

**You would want it** if your TV offers only this form and you prefer Dolby Vision to HDR10. On some TVs, leaving game mode brings back Dolby Vision Standard, which is better.

**Where.** *Settings > System > Display > Dolby Vision for TVs in game mode (Low-Latency)*. Off by default: such an input gets HDR10 and a notice.

### Older TVs (HDMI 1.4b)

**What it does.** Some TVs of 2016 (LG OLEDs among them) ask for an older way of signalling Dolby Vision. Hades can use it.

**Where.** *Dolby Vision for older TVs (HDMI 1.4b)*. Shown only for a TV that asks for it. Off by default. If the colours look wrong, turn it off.

### Dolby Vision sent as composed

**What it does.** The Dolby Vision picture of profiles 7 and 8 goes to the TV exactly as the composer of the specification makes it, in its own colours, bit for bit. Measured over the whole colour of every pixel, that is as close to the film as the other way, IPT, or closer, however the TV fills in the colour between the pixels. IPT, which licensed players are reported to send, stays as an expert choice.

**Where.** *Settings > System > Display > Dolby Vision engine > Transport*, Expert. New in 1.0, not tried on a TV.

### Dolby Vision shown as HDR10

**What it does.** When Dolby Vision cannot be sent (a display without it, or a TV input in game mode), Hades rebuilds the picture from the Dolby Vision data and sends it as HDR10. Profile 5 is always rebuilt, because its base picture cannot be watched on its own. Profile 7 and 8.1 normally play their own HDR10 picture; two settings rebuild them too.

**Where.** *Show Dolby Vision as HDR10 when the display cannot* (on by default), *Also with an enhancement layer*, and *Send it as HDR10+ where the display takes it*.

### Projectors and displays that map HDR poorly

**What it does.** A projector is much dimmer than a TV, and many map HDR badly. Hades can do the mapping in the PC, to the peak brightness, the colours and the black that you set: for Dolby Vision shown as HDR10 (*Adapt Dolby Vision to my display*), for HDR10 and HDR10+ (*HDR10 and HDR10+ adapted to the display*, which follows the HDR10+ data of each scene), and for Low-Latency (its manual peak, colours and black). *Black of the display* lifts the darkest parts of the film above a projector's own black. *Dolby Vision: ignore the studio's fine-tuning (trims)* stops sending the trims to a display that shows washed-out blacks. *HDR10: limit the luminance told to the display* tells the display a lower peak. *Play HDR videos as SDR* sends HLG, or every HDR video, as SDR to a display whose SDR picture you prefer.

**Where.** All at Expert level. The [settings guide](settings.md#for-projectors-and-displays-that-map-hdr-poorly) has a short section for projectors.

**Trims.** Where Hades maps the picture to the display, *Dolby Vision: apply the trims when adjusting to your display* (Expert, off) applies the colourist's trims made for your display's peak, in the only way the standards publish. Measured against a studio's own version of a film for a 1000-nit display, that way made the shadows much darker than the studio did, so it is off. Native Dolby Vision is not affected: the TV applies the trims itself. New in 1.0, not tried on hardware.

### HDR10, HDR10+ and HLG sent as Dolby Vision

**What it does.** For a TV that takes Dolby Vision but not HDR10+, an HDR10+ video can be sent as Dolby Vision, with metadata made from its HDR10+ data. An HDR10 video can be sent as Dolby Vision with metadata that Hades measures from the picture as it plays. An HLG video can be sent as Dolby Vision, with its picture converted to PQ as ITU-R BT.2100 describes. Each is off by default, and each changes the picture on purpose: the TV then shows it with its Dolby Vision processing. These are not the studio's metadata. Where an output cannot send HDR10 or HLG at all but can carry Dolby Vision (as the HDMI of some Intel graphics), Hades does it on its own, so that the video keeps its HDR instead of being mapped to SDR.

**Where.** *Play HDR10+ videos as Dolby Vision*, *Make HDR10 videos Dolby Vision (measured)*, *HLG as Dolby Vision*.

### HDR converted to SDR

**What it does.** On a display without HDR, Dolby Vision and HDR videos are mapped to SDR with the reference method of ITU-R BT.2390: everything the display can show goes out as it was mastered, and only what is brighter is squeezed in. You can say what kind of display the SDR is made for: a reference display (white at 203 nits), a bright room (300 nits), a projector (100 nits), or Custom, with the white and the gamma of your own display. If you preferred the look of before 1.0, with darker middle tones and a little more detail in the brightest highlights, choose "BT.2390, libplacebo's knee" as the tone mapping method of the video.

**Where.** *HDR to SDR: the display it is made for*. Reference by default. New in 1.0: the knee of the ITU-R report, Custom, and the name of the setting, which always applied to HDR10, HDR10+ and HLG as well. Measured with the software bridge.

### Videos with both Dolby Vision and HDR10+

**Where.** *Videos with both Dolby Vision and HDR10+*: Dolby Vision by default, or HDR10+. It also chooses between two video streams of one picture in a file, one with Dolby Vision and one with HDR10+. For one video, *Dolby Vision output (this video)* can choose either.

### Tone mapping v4.0

**What it does.** Dolby Vision data has an older (2.9) and a newer (4.0) tone-mapping part. TVs with newer processing use the 4.0 part. Hades can add a neutral 4.0 part where a video has none, or leave it out for a TV that shows a wrong picture with it. This changes what the TV makes of the picture.

**Where.** *Dolby Vision: new tone mapping (v4.0)*, Expert. If your TV states its peak brightness wrongly, *Tone mapping v4.0: set the TV's peak by hand* fixes what the automatic choices decide.

## HDR and the signal

### The refresh rate that Kodi would choose

**What it does.** Dolby Vision never changes the refresh rate that Kodi would choose. Progressive video gets its exact rate: 25 fps at 25 Hz, 29.97 fps at 29.97 Hz. On NVIDIA, the modes at exactly 24, 30, 60 and 120 Hz that the driver did not offer are added. With *Whole-number multiples of the frame rate*, 24 fps can play at 120 Hz without a change of mode.

### HDR10+

**What it does.** Sends HDR10+ with the metadata of each frame, where the graphics driver can send it. Where it cannot, the video goes out as HDR10. Nothing to set.

### Colour depth and format

**What it does.** At 4K above 30 Hz, an HDMI 2.0 link carries RGB at only 8 bits, so HDR would lose two bits. Hades sends HDR in a format that carries 10 or more (YCbCr). On NVIDIA, 10-bit video goes out through the 16-bit path, without banding. New in 1.0: *Colour format* and *Maximum bit depth* choose these by hand where the driver allows it, and a depth that another program left on the output is put back when Hades starts. Both at Expert level, Automatic by default.

### HDR above 8 bits on NVIDIA

**What it does.** Where an NVIDIA card would send HDR over HDMI in RGB at only 8 bits (the GeForce GTX 10, GTX 16 and RTX 20 cards, the TITAN V, and newer cards at 3840x2160 and 50 or 60 Hz without an HDMI 2.1 link), Hades sends it as YCbCr 4:2:2 at 12 bits (10 on the GTX 10 cards and the TITAN V), at the same link speed: less banding, and all the colour detail of the video. If the TV does not lock onto it, that mode goes back to 8 bits by itself.

**Where.** *HDR at more than 8 bits where the card sends RGB at 8 (NVIDIA)*, Expert, on by default since 1.0. Seen working at 10 bits on a GeForce GTX 1060; at 12 bits, not tried on hardware.

### HDR in 4:2:2 at 12 bits on Intel and AMD

**What it does.** At 3840x2160 and 50 or 60 Hz, Intel graphics and some AMD graphics (such as those of the Ryzen 7000 series) sent HDR in YCbCr 4:2:0, which halves the colour detail of the menus and subtitles drawn over the video. Hades now sends HDR10, HLG and HDR10+ there as YCbCr 4:2:2 at 12 bits, inside the same signal that carries Dolby Vision, where the TV takes 4:2:2. The video arrives exactly as decoded, and the text over it keeps its colour edges.

**You would notice** sharper colour at the edges of subtitles and menus over an HDR video at 50 or 60 Hz, and one more change of picture mode of the TV as such a video starts.

**Where.** *Settings > System > Display > HDR in 4:2:2 at 12 bits over HDMI 2.0*, Advanced, on by default, shown only where the driver offers it. If the graphics cannot keep up, Hades goes back to the ordinary output by itself. New in 1.0, not tried on hardware.

### The output after Dolby Vision

**What it does.** Native Dolby Vision changes how the graphics drive the TV (depth, colour format, range and more). When the video ends, Hades puts back everything that it changed, checks that the driver took it, and uses the colour settings that you have at that moment, also if you changed them during the video. If Kodi stops during Dolby Vision, the next start puts the output back. Nothing to set. New in 1.0, not tried on hardware.

### Dolby Vision on more graphics and connections

**What it does.** New in 1.0: native Dolby Vision on the newest Ryzen processors (display hardware DCN 4.2 and 4.2B), checked by reading the display pipe back each time it starts, and through a passive DisplayPort adaptor (DP++) that identifies itself as HDMI, on Intel and AMD graphics (NVIDIA already did). The Radeon RX 400, RX 500 and Vega cards get Dolby Vision only with *Dolby Vision on this Radeon (experimental)*, and the older Intel HD and UHD 6xx graphics can send HDR over HDMI with *HDR over HDMI on this Intel graphics*: both expert settings, shown only on that graphics, kept in the boot menus. *Check the Dolby Vision output* (*Settings > LibreELEC > System > Submit Log*) measures whether an AMD or Intel graphics card sends the Dolby Vision picture unchanged. None of these has been tried on hardware.

### Dolby Vision through a DisplayPort-to-HDMI converter

**What it does.** The HDMI socket of many mini PCs and NUCs is behind a converter that Linux treats as a DisplayPort output. Most converters do not carry Dolby Vision. Hades uses it only on the boards where it was seen working, unless you tell it to try.

**Where.** *Dolby Vision through a display converter*, Expert level, Automatic by default. Not tried on hardware.

### Picture in step with its sound

**What it does.** On every driver, the picture left the PC one refresh before its sound: 42 ms at 24 Hz. Hades now measures when each frame really reaches the screen and shows it with its sound. Which frames are seen, and for how long, does not change. If you had set *Audio offset* to make up for it, set it back to 0.

**Where.** *Settings > Player > Videos > Picture in step with its sound*, Advanced, on. New in 1.0, tested on the host.

## Performance

### Integrated graphics

**What it does.** Where the graphics need most of the time of a frame to prepare a Dolby Vision picture (typically integrated graphics with 4K Blu-ray video), the next frame is prepared while the last one waits, and the picture under menus and subtitles is redrawn only where they are. If even that is not enough, an automatic step-down rebuilds the picture slightly less precisely, only as far as the video needs to play smoothly. Graphics that keep up always show the exact picture.

**Where.** *Present frames a frame ahead*, *Fidelity step-down* and the other settings of the Dolby Vision engine group, all Expert. Leave them at Automatic unless a row of the settings guide says otherwise.

### A lighter interface

**What it does.** While Dolby Vision, HDR10 or HLG video plays, the menus are drawn again only where something changed. Opening the player information no longer redraws the whole interface for the values that change with every packet: they move twice a second. Dolby Vision made from HDR10, HDR10+ and profile 8.1 takes about a third less GPU time, and the colour table that a mapping to the display needs is made ahead, so the first picture no longer waits for it.

## Sound

### Passthrough and Dolby Atmos

**What it does.** TrueHD with Atmos, Dolby Digital Plus with Atmos and DTS-HD go to the receiver untouched. This is Kodi's own passthrough, kept as in LibreELEC. Hades adds a few things around it: sound that Hades decodes goes out in no more channels than your TV says it takes, so that a multichannel track does not go silent; where a TV takes only stereo PCM but also takes Dolby Digital, surround sound is encoded to Dolby Digital Plus 5.1 so it stays surround; and *Dialogue normalisation at 0 dB in passthrough* (off) stops the receiver from lowering the sound.

**Also.** Dynamic range compression for Dolby Digital, the level of the dialogue when 5.1 plays on fewer speakers, and the choice of the audio output that follows the screen you use.

### Passthrough through a pause and a seek

**What it does.** A pause keeps passthrough sound (TrueHD, Dolby Digital Plus, Dolby Digital, DTS): it goes on where it stopped, in sync, and the receiver stays on the stream while paused. After a start or a seek, the sound of TrueHD comes back after about 320 ms instead of 680. Several faults of DTS-HD passthrough that could stop the sound or close Kodi are fixed. Nothing to set. New in 1.0, measured on the host, not tried with a receiver.

### Keeping an NVIDIA card at full speed during HD audio

**What it does.** On a laptop on its charger, the sound of TrueHD or DTS-HD could cut out every few seconds, because the graphics card changed speed. This setting holds the card at full speed while that sound plays.

**Where.** *Keep the graphics card at full speed while HD audio plays through*. Off by default; shown only with an NVIDIA card. Not tried on hardware.

## Discs, 3D and files

### Blu-ray Java menus

**What it does.** Disc menus written in Java work with nothing to install. They run in a service apart from Kodi, with no network. With *BD-Live: Internet for disc menus* on, the disc's menus can reach the Internet and nothing else.

### 3D Blu-ray

**What it does.** Decodes both eyes of a 3D Blu-ray and switches a 3D display to 3D over HDMI. It needs a display that says it takes 3D. I have no 3D display, so this has been seen only by a user. In 1.0 the lines between the two eyes of frame packing are sent black, and the second view stays right after a picture that empties the decoder; neither has been seen on a 3D display yet.

**Where.** *Decode both views of 3D Blu-ray video (MVC)* and *Switch the display to 3D (HDMI)*, both Expert.

### Blu-ray jumps and seeks

**What it does.** Where a part of a Blu-ray starts at a picture that is not a key picture (the next part of a playlist, or where a menu jumps to), the pictures before it are decoded and not shown, so it plays from its first picture instead of up to a second later. After a jump or a seek in a film with a Dolby Vision full enhancement layer, the layer is there from the first picture. Chapter marks, joins and subtitles around a jump are kept right. The audio or subtitle stream that you choose is the one that plays, and the menus of an Ultra HD Blu-ray are drawn as the disc made them for its HDR picture, not grey. A disc image over the network opens with its menu, and faster. New in 1.0, not tried with a disc.

### Several editions in one file

**What it does.** Some Matroska files hold more than one cut of a film. Hades can ask which one to play. **Where:** *Ask which edition when a file has several*. Off by default.

### Picture subtitles

**What it does.** Dims picture-based subtitles (PGS, VobSub) that are too bright, makes their colours paler if they are too vivid, resizes them, and moves them to where the text subtitles go. *Bitmap subtitles made for* fixes subtitles made for SDR in an HDR film (they glare) or the other way round (they look grey). A subtitle now shows with its own frame, not one late. **Where:** *Player > Subtitles > Image based subtitles*.

### DVD and telecined films

**What it does.** A film on a DVD that is coded at 23.976 pictures per second with the flags to show it at 29.97 is shown at 23.976 Hz, each picture once, instead of in uneven steps. Hades tells it before the first picture, from the flags of the stream: in a file by what most of it is, and in a DVD image (ISO) or `VIDEO_TS` folder title by title. A disc in a drive is not read ahead, so it plays as before. Seeks in Matroska files start at the earliest key frame.

A film stored as interlaced video (3:2 pulldown on NTSC discs, a PAL film marked interlaced, a film at 25 in 29.97 frames) was deinterlaced at 59.94 or 50 Hz, softer and with uneven motion. With *Rebuild films from interlaced video* (*Settings > Player > Videos*, on), Hades looks at a few seconds at four places of a DVD title or an MPEG-2 file before it plays; where all of them show the same film, each picture is put back together from its own fields and the film plays at its own rate. Real interlaced video, mixed titles and anything uncertain play as before. The idea is CorentinB's. New in 1.0, not tried on hardware.

### Streams and the library

**What it does.** A network or a streaming service that stops sending for a while no longer ends the film as if it had been watched: Hades waits, says "Connection interrupted", and goes on from where it stopped when the data is back (up to 5 minutes). A second video asked for while another opens no longer waits for it. When a video ends, the library keeps what its playback found of its HDR (the Dolby Vision profile and layer, HDR10+), also for a `.strm`, a disc or a stream, so the skin can show it. New in 1.0, tried in a virtual machine.

## The system

### Safe defaults

Hades is made for a PC that also holds your other disks and your Windows. The internal disks are hidden when it runs from USB, file sharing is off until you turn it on and then has a password, and the update server of LibreELEC is not used. Every one of these can be turned back on. The README lists them with the reason in [Defaults that differ from LibreELEC](../README.md#defaults-that-differ-from-libreelec).

### The Windows installer

**What it does.** Writes the image to a USB drive or a secondary disk, updates it without touching your settings, and removes it. It shows each drive's model and serial number, asks you to type a word first, and checks what it wrote. It is available in English and Spanish.

### Boot menu

**What it does.** Four entries (Run, Run with the internal disks read-only, Live, Installer), a wait of 5 seconds by default (10, 5, 3 or 1, in *Settings > LibreELEC > System > Boot menu*, kept by every update), and no second menu after an update. Run and Live keep each device to its own memory where the PC has an IOMMU.

### HDMI-CEC

**What it does.** The remote of the TV controls Kodi over HDMI-CEC with a Pulse-Eight USB adapter, as in LibreELEC. New in 1.0: also through a DisplayPort-to-HDMI adapter that carries CEC over the DisplayPort cable, and on SECO boards (UDOO x86); the keys work sooner after a start where an amplifier answers slowly, and faults of libCEC that could close Kodi are fixed. The red key of the remote opens the player information. Not tried on hardware.

### PCs with several graphics chips, and laptops

**What it does.** Kodi starts on the TV, whichever chip it is connected to. If you connect the TV later, it moves there: on the same graphics without a restart (it asks first, and goes after a few seconds without an answer), on other graphics with one. *Settings > System > Display > Monitor* picks the display by hand. Outputs that Kodi does not draw on are turned off, except the one whose sound Kodi uses.

### The diagnostic report and switch

**What it does.** *Create a diagnostic report* makes one file, at once, with what is needed to find out why something does not work, every part of it through a filter that takes out what is private, and offers to copy it to a USB drive. *Save diagnostics at every start* keeps such a record of each start, for problems that only a start shows. Both send nothing anywhere. **Where:** *Settings > LibreELEC > System > Submit Log*. See [diagnostics](diagnostics.md).

### For add-on authors

`Player.Process(video.sidedata)` gives the Dolby Vision and HDR payloads of the picture on screen, in the same format as the CoreELEC builds, so that add-ons that read them work. `Player.Process(videooutputhdrtype)` and the output transfer function now report what the graphics driver holds, not what Kodi asked for. Nothing is collected until an add-on reads the label.
