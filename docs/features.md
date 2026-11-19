# Extras of Hades

What LibreELEC Hades adds to LibreELEC, one by one, in plain words. Each entry says what it does, when you would want it and where to control it. What has been seen on which hardware is in the [README](../README.md#what-has-been-tested). For the exact behaviour of a setting, see the [settings guide](settings.md). For how a video travels to the TV, see [how it works](how-it-works.md).

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

**What it does.** Opens a panel over the video. Its first line is a sentence: either that the video plays as intended, or what is wrong and what to do. Below it, the path of the signal: what the film carries, what Hades does with it, and what reaches your TV. More pages hold the details: the video, every level of the Dolby Vision metadata and which of them are used and sent, the sound with a plan of the speakers, and the system, with how the TV is connected (the link and its speed, the depth, whether variable refresh rate is on) and the versions of Hades, of the graphics driver and of the kernel. A bar at the bottom of every page gives the use and temperature of the processor and of the graphics, the memory, the pictures dropped and skipped, and the button *Output of this video*.

![The overview of the player information: the film, Hades and your TV, with a bar at the bottom](images/player-information.png)

**You would want it** to check that Dolby Vision is really on, or to see why it is not, and to change how a video goes out while it plays: O, then OK, opens the choice of output.

**Where.** Press O on a keyboard, or the red key of a TV remote that sends keys over HDMI-CEC. There is also a button in the video's on-screen controls. Down from the tabs goes to the button, with a remote, a keyboard or Yatse.

**Status.** Seen working on an RTX 4080 SUPER with an LG C5 (author), and, with changing the output from the on-screen controls, on a Ryzen 7 5800H (user); see the [README](../README.md#what-has-been-tested).

### Check this setup

**What it does.** Plays short test clips that the build of Hades makes itself: a test picture in SDR, HDR10, HLG and Dolby Vision (profiles 8.1 and 5), a soft noise from each speaker in turn, and a chime in each sound format that a receiver or TV may take as it is (Dolby Digital, Dolby Digital Plus, Dolby TrueHD). After each one it says in plain words what went out (the display mode, the HDR signal, whether Dolby Vision went out natively and why not, the sound format), asks what you saw or heard, and says what to do if it was wrong: for example, that pink and green colours mean the TV did not switch to Dolby Vision, and that a receiver in between may be the cause. On the SDR picture it also asks how many of the dark and light squares you can see, which tells whether the black level and the contrast of the TV are right.

**You would want it** after installing, after changing the TV or the receiver, or when a sound format plays silence. It switches on only the sound formats that you heard, and switches one off only if you say so; nothing of the picture settings changes. DTS cannot be checked this way.

**Where.** *Settings > LibreELEC > System > Check this setup*. *Show the last result* shows what it found last time. A user ran it on an RX 550 with an LG G3 (issue #20), where it showed which of the paths broke on that card.

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

**Displays whose values cannot be used.** Where the peak or the colours that the display gives for Low-Latency cannot be used, a display that also takes Standard gets Standard, and one that offers only Low-Latency gets the video rebuilt as HDR10, enhancement layer included. Colours that a display gives past BT.2020 (a laser projector) are taken to the edge of BT.2020 instead of refusing Low-Latency. *Low-Latency: use the black level the display gives* (Expert, on) lifts the darkest parts of the film to the black that the display gives, so that they do not sink into it; most OLED TVs give 0, and nothing changes. Not tried on hardware yet: it needs a projector or an LCD TV.

### Older TVs (HDMI 1.4b)

**What it does.** Some TVs of 2016 (LG OLEDs among them) ask for an older way of signalling Dolby Vision. Hades can use it.

**Where.** *Dolby Vision for older TVs (HDMI 1.4b)*. Shown only for a TV that asks for it. Off by default. If the colours look wrong, turn it off.

### The output of one video

**What it does.** Chooses, for the video that plays, how it goes out: as the settings say, as native Dolby Vision, as HDR10 or HDR10+, without Dolby Vision, or "As SDR (without HDR)", for a projector or a TV whose SDR picture you prefer for that film. An HLG video gets the choice too ("HLG as Dolby Vision", "Without Dolby Vision", "As SDR"). The choice is remembered for the file.

**Where.** *Output of this video*, the button of the player information (O, then OK), the button of the video's on-screen controls, or the video settings of the player. For a video that is not Dolby Vision it is called *HDR output (this video)*.

### Dolby Vision sent as composed

**What it does.** The Dolby Vision picture of profiles 7 and 8 goes to the TV exactly as the composer of the specification makes it, in its own colours, bit for bit. Measured over the whole colour of every pixel, that is as close to the film as the other way, IPT, or closer, however the TV fills in the colour between the pixels. IPT, which licensed players are reported to send, stays as an expert choice.

**Where.** *Settings > System > Display > Dolby Vision engine > Transport*, Expert.

### Dolby Vision shown as HDR10

**What it does.** When Dolby Vision cannot be sent (a display without it, or a TV input in game mode), Hades rebuilds the picture from the Dolby Vision data and sends it as HDR10. Profile 5 is always rebuilt, because its base picture cannot be watched on its own. Profile 7 and 8.1 normally play their own HDR10 picture; two settings rebuild them too.

**Where.** *Show Dolby Vision as HDR10 when the display cannot* (on by default), *Also with an enhancement layer*, and *Send it as HDR10+ where the display takes it*.

### Projectors and displays that map HDR poorly

**What it does.** A projector is much dimmer than a TV, and many map HDR badly. Hades can do the mapping in the PC, to the peak brightness, the colours and the black that you set: for Dolby Vision shown as HDR10 (*Adapt Dolby Vision to my display*), for HDR10 and HDR10+ (*HDR10 and HDR10+ adapted to the display*, which follows the HDR10+ data of each scene), and for Low-Latency (its manual peak, colours and black). *Black of the display* lifts the darkest parts of the film above a projector's own black. *Dolby Vision: ignore the studio's fine-tuning (trims)* stops sending the trims to a display that shows washed-out blacks. *HDR10: limit the luminance told to the display* tells the display a lower peak. *Play HDR videos as SDR* sends HLG, or every HDR video, as SDR to a display whose SDR picture you prefer.

**Where.** All at Expert level. The [settings guide](settings.md#for-projectors-and-displays-that-map-hdr-poorly) has a short section for projectors.

**Trims.** Where Hades maps the picture to the display, *Dolby Vision: apply the trims when adjusting to your display* (Expert, off) applies the colourist's trims made for your display's peak, in the only way the standards publish. Measured against a studio's own version of a film for a 1000-nit display, that way made the shadows much darker than the studio did, so it is off. Native Dolby Vision is not affected: the TV applies the trims itself.

### HDR10, HDR10+ and HLG sent as Dolby Vision

**What it does.** For a TV that takes Dolby Vision but not HDR10+, an HDR10+ video can be sent as Dolby Vision, with metadata made from its HDR10+ data. An HDR10 video can be sent as Dolby Vision with metadata that Hades measures from the picture as it plays. An HLG video can be sent as Dolby Vision, with its picture converted to PQ as ITU-R BT.2100 describes. Each is off by default, and each changes the picture on purpose: the TV then shows it with its Dolby Vision processing. These are not the studio's metadata. Where an output cannot send HDR10 or HLG at all but can carry Dolby Vision (as the HDMI of some Intel graphics), Hades does it on its own, so that the video keeps its HDR instead of being mapped to SDR.

**Where.** *Play HDR10+ videos as Dolby Vision*, *Make HDR10 videos Dolby Vision (measured)*, *HLG as Dolby Vision*.

### HDR converted to SDR

**What it does.** On a display without HDR, Dolby Vision and HDR videos are mapped to SDR with the reference method of ITU-R BT.2390: everything the display can show goes out as it was mastered, and only what is brighter is squeezed in. You can say what kind of display the SDR is made for: a reference display (white at 203 nits), a bright room (300 nits), a projector (100 nits), or Custom, with the white and the gamma of your own display. If you preferred the look of before 1.0, with darker middle tones and a little more detail in the brightest highlights, choose "BT.2390, libplacebo's knee" as the tone mapping method of the video.

**Where.** *HDR to SDR: the display it is made for*. Reference by default. It applies to HDR10, HDR10+ and HLG as well, with the knee of the ITU-R report or a Custom display. Measured with the software bridge. A profile that still has Kodi's tone mapping method "Off", which left HDR washed out on a display without HDR, is switched to "Reference (BT.2390)" once.

**HDR10 mapped scene by scene** (*System > Display*, Expert, off). Where Hades maps an HDR10 video itself (to SDR, or adapted to a projector), it can measure each scene as it plays and map it by its own brightest and average light, as it does for Dolby Vision, instead of by the one brightness of the whole film. A dark scene keeps more of its highlights, and the mapping changes slowly within a scene, so the picture does not pump.

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

**What it does.** At 4K above 30 Hz, an HDMI 2.0 link carries RGB at only 8 bits, so HDR would lose two bits. Hades sends HDR in a format that carries 10 or more (YCbCr). On NVIDIA, 10-bit video goes out through the 16-bit path, without banding. *Colour format* and *Maximum bit depth* choose these by hand where the driver allows it, and a depth that another program left on the output is put back when Hades starts. Both at Expert level, Automatic by default.

### HDR above 8 bits on NVIDIA

**What it does.** Where an NVIDIA card would send HDR over HDMI in RGB at only 8 bits (the GeForce GTX 10, GTX 16 and RTX 20 cards, the TITAN V, and newer cards at 3840x2160 and 50 or 60 Hz without an HDMI 2.1 link), Hades sends it as YCbCr 4:2:2 at 12 bits (10 on the GTX 10 cards and the TITAN V), at the same link speed: less banding, and all the colour detail of the video. If the TV does not lock onto it, that mode goes back to 8 bits by itself.

**Where.** *HDR at more than 8 bits where the card sends RGB at 8 (NVIDIA)*, Expert, on by default. Seen working by users at 10 bits on a GeForce GTX 1060, and in YCbCr 4:2:2 at 12 bits on a GeForce GTX 1650 and a Quadro RTX 4000.

### HDR in 4:2:2 at 12 bits on Intel and AMD

**What it does.** At 3840x2160 and 50 or 60 Hz, Intel graphics and some AMD graphics (such as those of the Ryzen 7000 series) sent HDR in YCbCr 4:2:0, which halves the colour detail of the menus and subtitles drawn over the video. Hades now sends HDR10, HLG and HDR10+ there as YCbCr 4:2:2 at 12 bits, inside the same signal that carries Dolby Vision, where the TV takes 4:2:2. The video arrives exactly as decoded, and the text over it keeps its colour edges.

**You would notice** sharper colour at the edges of subtitles and menus over an HDR video at 50 or 60 Hz, and one more change of picture mode of the TV as such a video starts.

**Where.** *Settings > System > Display > HDR in 4:2:2 at 12 bits over HDMI 2.0*, Advanced, on by default, shown only where the driver offers it. If the graphics cannot keep up, Hades goes back to the ordinary output by itself.

### The output after Dolby Vision

**What it does.** Native Dolby Vision changes how the graphics drive the TV (depth, colour format, range and more). When the video ends, Hades puts back everything that it changed, checks that the driver took it, and uses the colour settings that you have at that moment, also if you changed them during the video. If Kodi stops during Dolby Vision, the next start puts the output back. Nothing to set.

### Dolby Vision on more graphics and connections

**What it does.** Native Dolby Vision on the newest Ryzen processors (display hardware DCN 4.2 and 4.2B), checked by reading the display pipe back each time it starts, and through a passive DisplayPort adaptor (DP++) that identifies itself as HDMI, on Intel and AMD graphics (NVIDIA already did). An adaptor that does not say HDMI gets no Dolby Vision, on NVIDIA too. The Radeon RX 400, RX 500 and Vega cards get Dolby Vision only with *Dolby Vision on this Radeon (experimental)*, and the older Intel HD and UHD 6xx graphics can send HDR over HDMI with *HDR over HDMI on this Intel graphics*: both expert settings, shown only on that graphics, kept in the boot menus. *Check the Dolby Vision output* (*Settings > LibreELEC > System > Submit Log*) measures whether an AMD or Intel graphics card sends the Dolby Vision picture unchanged, and says which colour of the output changes; where it finds a card not exact, Hades uses neither native Dolby Vision nor HDR in 4:2:2 at 12 bits on that output. The check was run by a user on an RX 550 (issue #20).

### Dolby Vision through a DisplayPort-to-HDMI converter

**What it does.** The HDMI socket of many mini PCs and NUCs is behind a converter that Linux treats as a DisplayPort output, and so is the HDMI socket of a USB-C hub or adapter, which is how a Chromebook reaches a TV. Most converters do not carry Dolby Vision. Hades uses one only on the boards where it was seen working, unless you tell it to try. The converter is found by asking the port itself, so a hub or adapter is offered too, and the kernel log names the converter.

**Where.** *Dolby Vision through a display converter*, Expert level, Automatic by default, shown only where such a converter is behind the port. Not tried on hardware yet.

### SDR videos in wide colours

**What it does.** A few SDR videos (some Ultra HD Blu-rays and streams) are made in the wider colours of BT.2020. Kodi converts them to the ordinary colours of BT.709 and clips what lies outside. With *Keep the wide colours of SDR videos (BT.2020)* on, on a display whose EDID says that it takes BT.2020, they go out as they are, and the menus and subtitles are converted to match so that they keep their usual colours. The ordinary conversion to BT.709 keeps the greys and the black of the video.

**Where.** *Settings > System > Display*, Advanced, off.

### Upscalers

**What it does.** When a video is smaller than your screen (1080p on a 4K TV, a DVD on a 1080p one), Kodi scales it up with a filter. An upscaler redraws it instead and rebuilds edges and fine lines: RAVU is the lightest, FSRCNNX 8 gives more detail, and FSRCNNX 16 the most and needs a strong graphics card. Measured against Kodi's Lanczos 3, they come closer to the original picture by 0.1 to 1.3 dB, 0.3 to 3.4 dB and 0.4 to 4.7 dB. They change the picture, so they are off unless you choose one; they are never used for Dolby Vision, and if the graphics cannot keep up, the upscaler turns itself off for that video and a message tells you.

**Where.** *Settings > Player > Videos*, Expert: *Upscaler for videos of 1080p and above*, *Upscaler for videos below 1080p*, and *Upscaler for animation* for the videos whose genre says Animation or Anime. Or, while a video plays, the button of the Video page of the player information (O): it names the upscaler of the videos like the one that plays and changes it, for all of them (OK on the button, or on the Video tab).

### Video at its own rate with VRR

**What it does.** Each video normally changes the mode of the display to its own rate, and the screen goes black for a moment as it starts and stops. On a TV with variable refresh rate (VRR), Hades can play an SDR or HDR10 video at its own rate in the mode of the menus instead, each picture shown at its time: 23.976 fps goes out at 47.952 Hz, each picture twice. It cannot go out at 23.976 Hz itself: no display takes VRR that low (an LG C5 from 40 Hz, a Samsung Odyssey G60SD from 48 Hz), so each picture goes twice, or three times at 71.928 Hz on the monitor, for exactly the same motion. On NVIDIA GeForce GTX 16 and newer the display engine holds each picture until its time; on the RTX 50 series Media VRR keeps the rate; on AMD graphics Kodi times each picture. On the RTX 40 and RTX 50 series HDR10+ goes too, and so does Dolby Vision that is sent as HDR10, HDR10+ or SDR to a display without Dolby Vision. Dolby Vision sent as Dolby Vision, HLG and 3D change the mode to the rate of the video as always: Dolby Vision needs modes of its own of up to 60 Hz, and no display says that it takes it with VRR.

**You would want it** if your monitor has no mode of 24, 48 or 72 Hz, so a film goes out at 120 or 360 Hz with a picture held a refresh longer now and then (choose *Where the display has no mode at the video's rate*); or if the black at the start of every video bothers you and your display has VRR (choose *Always*; on an LG TV, turn on "VRR & G-Sync" for NVIDIA graphics or "AMD FreeSync Premium" for AMD graphics). With VRR on, the display's own information shows the rate it gets (47 Hz on the LG C5, 72 Hz on the monitor), and the player information says the same, measured.

**Where.** *Settings > System > Display > Video at its own rate with VRR*, Expert: *Off* (the default), *Where the display has no mode at the video's rate*, or *Always*. On NVIDIA it leaves *HDMI variable refresh rate (VRR)* to the driver where it needs it. In the first second of each video Hades checks that the refresh really follows the video (on NVIDIA, as the driver says and by when each picture reached the output), and plays the video as with the setting off where it does not; the player information says which it was. `kodi.log` says how exact the timing was.

### Picture in step with its sound

**What it does.** On every driver, the picture left the PC one refresh before its sound: 42 ms at 24 Hz. Hades now measures when each frame really reaches the screen and shows it with its sound. Which frames are seen, and for how long, does not change. If you had set *Audio offset* to make up for it, set it back to 0.

**Where.** *Settings > Player > Videos > Picture in step with its sound*, Advanced, on.

## Performance

### Integrated graphics

**What it does.** Where the graphics need most of the time of a frame to prepare a Dolby Vision picture (typically integrated graphics with 4K Blu-ray video), the next frame is prepared while the last one waits, and the picture under menus and subtitles is redrawn only where they are. If even that is not enough, an automatic step-down rebuilds the picture slightly less precisely, only as far as the video needs to play smoothly. Graphics that keep up always show the exact picture.

**Where.** *Present frames a frame ahead*, *Fidelity step-down* and the other settings of the Dolby Vision engine group, all Expert. Leave them at Automatic unless a row of the settings guide says otherwise.

### A lighter interface

**What it does.** While Dolby Vision, HDR10 or HLG video plays, the menus are drawn again only where something changed. Opening the player information no longer redraws the whole interface for the values that change with every packet: they move twice a second. Dolby Vision made from HDR10, HDR10+ and profile 8.1 takes about a third less GPU time, and the colour table that a mapping to the display needs is made ahead, so the first picture no longer waits for it.

## Sound

### Passthrough and Dolby Atmos

**What it does.** TrueHD with Atmos, Dolby Digital Plus with Atmos and DTS-HD go to the receiver untouched. This is Kodi's own passthrough, kept as in LibreELEC. Hades adds a few things around it: sound that Hades decodes goes out in no more channels than your TV says it takes, so that a multichannel track does not go silent; where a TV takes only stereo PCM but also takes Dolby Digital, surround sound is encoded to Dolby Digital Plus 5.1 so it stays surround; and *Dialogue normalisation at 0 dB in passthrough* (off) stops the receiver from lowering the sound.

![The Audio page of the player information: the sound of the film, the sound that goes out, and the speakers on a plan of the room](images/player-information-audio.png)

**Also.** Dynamic range compression for Dolby Digital, the level of the dialogue when 5.1 plays on fewer speakers, and the choice of the audio output that follows the screen you use. Sound that Hades decodes goes out in 16 bits to a display that declares PCM of 16 or 20 bits only (one such TV played nothing), a 6.1 or 6.0 track keeps its back centre on a receiver that has that speaker, and Dolby Digital at 32 kHz is passed through.

### Surround sound through a USB-C adapter or hub

**What it does.** Some USB-C hubs and DisplayPort to HDMI adapters tell the computer that the TV takes stereo only, even when the TV or the sound bar behind them takes more. This setting tells the sound driver that what is behind the adapter takes PCM in 8 channels, Dolby Digital, Dolby Digital Plus and Dolby TrueHD. Use it only if your TV or sound bar takes them: if it does not, a film plays without sound.

**Where.** *Settings > System > Audio > Surround sound through this USB-C adapter or hub*, Expert, off, shown only where such an adapter is behind a port of Intel graphics. It takes effect from the next start. Not tried on hardware yet.

### Passthrough through a pause and a seek

**What it does.** A pause keeps passthrough sound (TrueHD, Dolby Digital Plus, Dolby Digital, DTS): it goes on where it stopped, in sync, and the receiver stays on the stream while paused. After a start or a seek, the sound of TrueHD comes back after about 320 ms instead of 680. Several faults of DTS-HD passthrough that could stop the sound or close Kodi are fixed. Nothing to set. Measured on the host.

### Keeping an NVIDIA card at full speed during HD audio

**What it does.** On a laptop on its charger, the sound of TrueHD or DTS-HD could cut out every few seconds, because the graphics card changed speed. This setting holds the card at full speed while that sound plays.

**Where.** *Keep the graphics card at full speed while HD audio plays through*. Off by default; shown only with an NVIDIA card. Not tried on hardware yet.

## Discs, 3D and files

### Blu-ray Java menus

**What it does.** Disc menus written in Java work with nothing to install. They run in a service apart from Kodi, with no network. With *BD-Live: Internet for disc menus* on, the disc's menus can reach the Internet and nothing else.

### 3D Blu-ray

**What it does.** Decodes both eyes of a 3D Blu-ray and switches a 3D display to 3D over HDMI. It needs a display that says it takes 3D. I have no 3D display; users have seen 3D Blu-ray video (MVC) and full side by side on a Ryzen 7 5800H, and frame packing on a Philips 3D TV from Intel graphics. In 1.0 the lines between the two eyes of frame packing are sent black, and the second view stays right after a picture that empties the decoder. Through an active DisplayPort or USB-C converter the graphics driver offers no 3D mode, and `kodi.log` says so.

**Where.** *Decode both views of 3D Blu-ray video (MVC)* and *Switch the display to 3D (HDMI)*, both Expert.

### Blu-ray jumps and seeks

**What it does.** Where a part of a Blu-ray starts at a picture that is not a key picture (the next part of a playlist, or where a menu jumps to), the pictures before it are decoded and not shown, so it plays from its first picture instead of up to a second later. After a jump or a seek in a film with a Dolby Vision full enhancement layer, the layer is there from the first picture. Chapter marks, joins and subtitles around a jump are kept right. The audio or subtitle stream that you choose is the one that plays, and the menus of an Ultra HD Blu-ray are drawn as the disc made them for its HDR picture, not grey. A disc image over the network opens with its menu, and faster.

### Several editions in one file

**What it does.** Some Matroska files hold more than one cut of a film. Hades can ask which one to play. **Where:** *Ask which edition when a file has several*. Off by default.

### Picture subtitles

**What it does.** Dims picture-based subtitles (PGS, VobSub) that are too bright, makes their colours paler if they are too vivid, resizes them, and moves them to where the text subtitles go. *Bitmap subtitles made for* fixes subtitles made for SDR in an HDR film (they glare) or the other way round (they look grey). A subtitle now shows with its own frame, not one late. **Where:** *Player > Subtitles > Image based subtitles*.

### DVD and telecined films

**What it does.** A film on a DVD that is coded at 23.976 pictures per second with the flags to show it at 29.97 is shown at 23.976 Hz, each picture once, instead of in uneven steps. Hades tells it before the first picture, from the flags of the stream: in a file by what most of it is, and in a DVD image (ISO) or `VIDEO_TS` folder title by title. A disc in a drive is not read ahead, so it plays as before. Seeks in Matroska files start at the earliest key frame.

A film stored as interlaced video (3:2 pulldown on NTSC discs, a PAL film marked interlaced, a film at 25 in 29.97 frames) was deinterlaced at 59.94 or 50 Hz, softer and with uneven motion. With *Rebuild films from interlaced video* (*Settings > Player > Videos*, on), Hades looks at a few seconds at four places of a DVD title or an MPEG-2 file before it plays; where all of them show the same film, each picture is put back together from its own fields and the film plays at its own rate. Real interlaced video, mixed titles and anything uncertain play as before. The idea is CorentinB's.

### Streams and the library

**What it does.** A network or a streaming service that stops sending for a while no longer ends the film as if it had been watched: Hades waits, says "Connection interrupted", and goes on from where it stopped when the data is back (up to 5 minutes). A second video asked for while another opens no longer waits for it. When a video ends, the library keeps what its playback found of its HDR (the Dolby Vision profile and layer, HDR10+), also for a `.strm`, a disc or a stream, so the skin can show it.

For streaming add-ons: a Blu-ray image plays as a disc even when the address of the add-on does not end in `.iso`; a server that allows one connection per file no longer stalls each seek (61 s before, under 2 s measured); an address whose token ran out is asked again of the add-on; disc images on the internet open faster; and a change of subtitle track shows at once, from what was read.

### The variety of a language

**What it does.** Some videos have the same language twice: Spanish from Spain (Castilian) and from Latin America, Portuguese from Portugal and from Brazil, French from France and from Canada, Chinese in simplified and in traditional characters. This says which one plays first, for the audio and for the subtitles. Automatic follows the region of your language settings (Spanish (Mexico) prefers Latin American Spanish); a track that you choose in a video is remembered for it and still comes first.

**Where.** *Settings > Player > Language > Variety of Spanish, Portuguese, French or Chinese*.

## The system

### Safe defaults

Hades is made for a PC that also holds your other disks and your Windows. The internal disks are hidden when it runs from USB, file sharing is off until you turn it on and then has a password, and the update server of LibreELEC is not used. Every one of these can be turned back on. The README lists them with the reason in [Defaults that differ from LibreELEC](../README.md#defaults-that-differ-from-libreelec).

### The Windows installer

**What it does.** Writes the image to a USB drive or a secondary disk, updates it without touching your settings, and removes it. It shows each drive's model and serial number, asks you to type a word first, and checks what it wrote. It is available in English and Spanish.

### Boot menu

**What it does.** Four entries (Run, Run with the internal disks read-only, Live, Installer), a wait of 5 seconds by default (10, 5, 3 or 1, in *Settings > LibreELEC > System > Boot menu*, kept by every update), and no second menu after an update. Run and Live keep each device to its own memory where the PC has an IOMMU.

Since 1.1, on a UEFI PC, the picture of the start (the eclipse over the name, on black) stays on the screen from the moment an entry starts until Kodi shows its interface, with no black box, cursor or line of text, on every display at its own size, also where the NVIDIA driver loads after the start; the line of the version shows only with an error. If Kodi stops three times in a row, every display shows a card that says so, with what to do and the version. Seen in virtual machines, not on a PC.

![The boot menu of LibreELEC Hades](images/boot-menu.png)

### HDMI-CEC

**What it does.** The remote of the TV controls Kodi over HDMI-CEC with a Pulse-Eight USB adapter, as in LibreELEC, and also through a DisplayPort-to-HDMI adapter that carries CEC over the DisplayPort cable, and on SECO boards (UDOO x86); the keys work sooner after a start where an amplifier answers slowly, and faults of libCEC that could close Kodi are fixed. The red key of the remote opens the player information. Through an adapter on an output that does not carry the picture (an AV receiver for the sound only), Kodi no longer switches the TV's input; turning CEC off takes the PC off the bus at once; and an adapter that comes back, or is plugged in later, is found without restarting Kodi.

### PCs with several graphics chips, and laptops

**What it does.** Kodi starts on the TV, whichever chip it is connected to. If you connect the TV later, it moves there: on the same graphics without a restart (it asks first, and goes after a few seconds without an answer), on other graphics with one. *Settings > System > Display > Monitor* picks the display by hand. Outputs that Kodi does not draw on are turned off, except the one whose sound Kodi uses, and one that the file `hades-keep-outputs` keeps on, which keeps its sound too.

**Chromebooks** with Intel graphics: the drivers of the ChromeOS embedded controller and of the DisplayPort mode of USB-C load on Chromebooks, so a hub or an adapter on the USB-C port gets a picture. Based on the work of Felix Oghină. Not tried on hardware yet.

### The clock of the PC

**What it does.** Hades never writes the clock of the PC, so Windows keeps the right time. Once the time has been taken from the internet, it measures whether that clock keeps local time, as Windows keeps it, or UTC, as a PC without Windows does, and reads it that way from the next start. **Where:** *Settings > LibreELEC > System > OS Configuration > Hardware clock*: Automatic by default, or "Local time" and "UTC" by hand.

### The diagnostic report and switch

**What it does.** *Create a diagnostic report* makes one file, at once, with what is needed to find out why something does not work, every part of it through a filter that takes out what is private, and offers to copy it to a USB drive. *Save diagnostics at every start* keeps such a record of each start, for problems that only a start shows; the boot menus then also have the kernel write a line for each driver that it starts, so a start that stops while drivers load leaves the driver it was in. Both send nothing anywhere. **Where:** *Settings > LibreELEC > System > Submit Log*. See [diagnostics](diagnostics.md).

### For add-on authors

`Player.Process(video.sidedata)` gives the Dolby Vision and HDR payloads of the picture on screen, in the same format as the CoreELEC builds, so that add-ons that read them work. `Player.Process(videooutputhdrtype)` and the output transfer function now report what the graphics driver holds, not what Kodi asked for. Nothing is collected until an add-on reads the label.
