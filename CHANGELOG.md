# Changelog

The versions of LibreELEC Hades. Before 1.0 RC1 there were only private test builds.

*Las versiones de LibreELEC Hades, cada una también en español. Antes de 1.0 RC1 solo hubo compilaciones de prueba privadas.*

## 1.0 RC3

### What's new for you

- **Menus over Dolby Vision cost far less.** With a menu, the OSD, the seek bar or subtitles over native Dolby Vision, each frame is now remade only where they are drawn, with the same bytes sent to the TV. On the integrated graphics of the Ryzen 9 7950X3D, the seek bar over a profile 7 film takes 17.8 ms of GPU time instead of 30.5, of the 41.7 ms that each frame has (measured through Direct3D 12 under Windows). The measurement of what is drawn over the black bars, which cost about 40 ms a frame on a Radeon RX 7600 and made films with a menu over them play at about 16 frames a second (issue #3), is now made in blocks. Profile 5, whose pixels under a menu cost the most, has a pass of its own for them.
- **Dolby Vision Low-Latency (player-led), as an option.** For a TV input that offers Dolby Vision only in its low-latency form (the game mode of some TVs, or a TV that has no other), Hades can now send it that way, mapping the picture to the display itself. Off by default. The mapping is Hades' own, not Dolby's.
- **HDR10+ to displays that take it.** The dynamic metadata of each frame of an HDR10+ video now go out with that frame to a display that declares HDR10+, on NVIDIA, AMD and Intel graphics, within the limits of each driver (see the details below).
- **Other formats as Dolby Vision, as options (off by default):** an HDR10+ video to a TV that takes Dolby Vision but not HDR10+, with metadata made from its own HDR10+ metadata; and an HDR10 video, with metadata that Hades measures from the picture as it plays and that say so: they are not the studio's.
- **For projectors:** Dolby Vision can be sent without its trims, for a display that shows washed-out blacks or wrong colours with them (off by default).
- **HDR and Dolby Vision on a display without HDR.** HDR10, HLG and Dolby Vision profiles 5, 7 and 8.1 are mapped to SDR as a reference display maps them (ITU-R BT.2390), now the default tone mapping method: measured, much closer to the master than Kodi's own methods. Profile 5 on such a display no longer comes out green and purple.
- **More Dolby Vision files play as Dolby Vision:** profile 7 in two video tracks of MKV and MP4 files (issues #1 and #5), and HEVC streams that carry Dolby Vision without a record of it in their container.
- **More TVs and graphics:** Dolby Vision TVs that read display management 2 or 3 (many of 2017 and 2018), Radeon RX 9000 graphics (RDNA 4), Intel UHD 6xx graphics, and the HDMI port of laptops whose NVIDIA graphics drive it through a dual-mode DisplayPort connector.
- **Better HDR10 on AMD and NVIDIA:** on AMD, HDR10 in YCbCr 4:2:2 at 12 bits where RGB would be 8 bits; on NVIDIA GeForce GTX 16 and RTX 20 graphics, HDR over HDMI at 12 bits instead of 8 where the mode fits (off by default: `nvidia-modeset.hades_turing_12bpc=1` on the kernel line turns it on).
- **Fewer black frames and display changes:** Dolby Vision shown as HDR10 keeps the last whole picture where one fails; the next video of a list at another frame rate changes the display once; the menus keep moving while a change of display waits; an NVIDIA output whose display stops answering is turned off and on until it comes back; and after a wake from suspend the mode is set again where NVIDIA's driver had left the output off (a laptop had no picture after a wake).
- **Sound:** PCM goes out in no more channels than the TV says it takes, also through a change of display mode (a TV that declared 2 channels of PCM could be sent more, the likely cause of a DTS-HD track that played no sound; an Expert setting turns it off); where the TV takes only stereo PCM but Dolby in more channels, a multichannel track that Kodi decodes goes out as Dolby Digital Plus or Dolby Digital 5.1 instead of stereo (a setting, on by default); and an HDMI audio output that is set but not there no longer keeps passthrough off.
- **The mouse and the menus:** the pointer keeps the fractions of a pixel of each motion (a slow movement moved it less than it should, or not at all), and motions that pile up are handled as one; when a film starts from an add-on, the menus keep drawing, with the busy dialog, while the server of the stream is asked what the stream is, instead of freezing for seconds; and `kodi.log` says once a minute when the interface was slow.
- **Choosing the monitor:** *System > Display > Monitor*, which LibreELEC hides, is shown again, with each display by its name. A change to a display of the same graphics takes effect at once; one on other graphics starts Kodi again. Either way Kodi asks whether to keep it, and goes back if not. Not while something plays.
- **Diagnostics that can be shared:** addresses, serial numbers, network names and the names of films become markers or are removed, in every file; the diagnostics can be copied to a USB drive, also with nothing on the screen, and an update with the installer keeps them.
- **Easier to live with:** a phone remote control in one step, a setting for a PC whose clock keeps UTC, a weekly trim of the storage partition, Hades' player information in any skin, with the GPU's use and temperature, and hardening measured to cost nothing in playback.
- **Up to date and safer:** LibreELEC and Kodi at their latest commits, libplacebo, LLVM, systemd and kernel firmware, and fixes for known security issues in FFmpeg, OpenSSH, Python, curl, BlueZ, libarchive and libtiff, with four fixes of Hades' own for libbluray, libdisplay-info and libdvdnav.

### Before you update

- **AMD graphics with a TV that cannot select the RGB range.** AMD graphics now send such a TV limited-range RGB, as the TV expects (the change is AMD's). If you turned on Kodi's *System > Display > Use limited colour range (16-235)* because blacks were crushed on such a TV, turn it off, or the range is compressed twice. A TV that selects the range, as the LG C5 does, gets what it got before.
- **Tone mapping.** A tone mapping method saved as Hable, which was Kodi's default, becomes "Reference (BT.2390)" once. Hable chosen again afterwards stays.

### Details

**Dolby Vision picture and metadata**

- The PQ curve of the Dolby Vision decoding is computed without cancellation (`libplacebo-9942`): a profile 5 picture rebuilt as HDR10 had bands of up to 315 codes of 4095 at the edge of BT.2020, now 39 at most.
- The mapping to a display (SDR, projectors, Low-Latency) maps to the display's peak, not to a virtual peak above it that darkened the whole picture by 2 to 11 % (`libplacebo-9943`), and its gamut table is finer (96x64x512 instead of 48x32x256), which halves the largest error on SDR.
- Menus or subtitles over the black bars no longer mark a new scene in the metadata (ETSI GS CCM 001, 6.2.2): a change of the active area takes a new metadata id instead, and a bar stays in the area for 2 s of video after what was drawn over it is gone.
- A display of display management 2 or 3 is sent the content mapping v2.9 part of the metadata, which it reads, without the content mapping v4.0 blocks, which it does not.
- A picture that goes out without its enhancement picture (after a seek, or late) carries the RPU of its own access unit, not that of a neighbour.
- The enhancement layer starts by picture order at a random-access picture of its own where the two layers do not share one; with the base layer reordered, it never started before. A new VAAPI device context of the same display no longer starts the layer again, and a MEL stream whose layer turns out to carry a residual loses it once at most, then has its layer decoded.
- Dolby Vision shown as HDR10 draws into a second texture and shows a picture only when it is whole (132 MB more at 3840x2160); where nothing rebuilt can be shown, it shows the base layer when that is a picture by itself. Its 8-bit limited-range copy has white at 235, not 234.
- Kodi's video scaling method is not used by the Dolby Vision engine, which resamples as the specification says; the Dolby Vision page of the player information now says so.

**Performance**

The times in this section were measured through Direct3D 12 under Windows, not with the Linux drivers.

- A frame with GUI elements over native Dolby Vision is remade only in the 16x16-pixel tiles that the GUI reaches, with the same bytes: on an RTX 4080 SUPER, 1.4 to 2.7 ms instead of 1.5 to 9.3. `<video><dvdrawntiles>false</dvdrawntiles></video>` in `advancedsettings.xml` turns it off, to compare.
- What is drawn over the bars is measured in blocks, each pixel read once: on the integrated graphics of the Ryzen 9 7950X3D, 5.5 ms instead of 19.6 under a dimmed screen.
- Profile 5 frames with the GUI over them: the pixels drawn over are converted in a pass of their own, each pair once and with the picture under them decoded once, three conversions where there were four, with the same bytes: about 60 ms instead of about 83 under the player information on that integrated graphics. Where a GPU cannot run the pass, the transport converts those pixels itself as before (the same bytes, more GPU time) and `kodi.log` says so.
- Where those profile 5 frames still miss refreshes (with *Fidelity step-down* at Automatic), their conversion goes up one level at a time, only as far as needed: first a faster way with the same bytes, then a simplified rounding that can leave a pixel drawn over one code off, which the player information then shows. Frames without GUI elements, profiles 7 and 8.1, and graphics that keep up are not affected.
- The presentation a frame ahead is not switched on or off by a seek, or by menus in the first moment after they appear.
- What the fidelity step-down finds is now remembered also with the default transport (it keeps a level for each set of levels a format can have): a slow GPU misses far fewer refreshes at the start of a video.

**Display and HDMI**

- Dolby Vision Low-Latency (`videoscreen.hadesdvlowlatency`, off): only on a display that offers Low-Latency and not Standard. The picture is mapped with the BT.2390 curve, guided by each scene's level 1 metadata, to the peak and primaries of the display's Dolby Vision block, and sent as YCbCr 4:2:2 at 12 bits, BT.2020, limited range, through the tunnel, with the low-latency vendor InfoFrame (`linux-9975` to `9977` for amdgpu, i915 and xe; `nvidia-open-kernel-modules-9030`). `<video><dvllpattern>true</dvllpattern></video>` shows a test pattern instead of the picture, for a first look at a display.
- HDR10+: the vendor InfoFrame of each frame goes out in the commit of that frame, never with a change of mode, through a new DRM property (`linux-9980`) with backends for amdgpu, i915, xe and NVIDIA. Fewer than 12 pictures without metadata keep the last InfoFrame; at 12 it goes back to HDR10. The graphics overlay flag is set while Kodi's GUI is over the video.
- HDR10+ over DisplayPort on NVIDIA Ada (RTX 40): the InfoFrame SDP is written without the hardware's checksum, which belongs to HDMI InfoFrames and in an SDP would take the place of the version byte (`nvidia-open-kernel-modules-9020`). The official HDR10+ Gaming FAQ says that on a PC HDR10+ goes over HDMI only.
- HDR10+ over HDMI on NVIDIA to a display that declares HDMI variable refresh, where the driver fills the generic InfoFrame slot with its own variable refresh data (from 50 Hz and 720 lines, whether or not variable refresh is in use) and HDR10+ was refused: Ada now sends it in shared generic slot 2, a slot that NVIDIA documents only for DisplayPort; GTX 16, RTX 20 and RTX 30 graphics, which have no other slot, still send none. A refusal says why in the kernel log (`Hades: HDR10+ refused on head N: ...`), and `kodi.log` says that the driver refused it.
- HDR10+ over HDMI on Intel at 3840x2160 at 24, 25 and 30 Hz and 4096x2160 at 24 Hz (and their 1000/1001 rates), where DRM puts the HDMI VIC in the vendor InfoFrame that HDR10+ also needs, so that a film at 23.976 Hz went out as HDR10 (issue #2): to an HDMI 2.0 display that lists the CTA VIC of the mode and declares HDR10+, the AVI InfoFrame now carries that VIC and the vendor InfoFrame carries HDR10+ (`linux-9982`). Everything else goes out as before, and a refusal says why in the kernel log.
- HDR10+ as Dolby Vision (`videoscreen.hadesdvfromhdr10plus`, off): profile 8.1 metadata made scene by scene from the HDR10+ metadata, with the scenes as hdr10plus_tool tells them and the defaults of dovi_tool; the average and the peak of each scene are taken by rules of Hades' own ([how it works](docs/how-it-works.md#other-formats-sent-as-dolby-vision)).
- HDR10 as Dolby Vision, measured (`videoscreen.hadesdvfromhdr10`, off): level 1 and the active area measured on the GPU from the pictures that Kodi has decoded ahead, and scene cuts found from the picture and marked as such (a few are missed, and a few are found where there is none); no trims and no level 3.
- Trims left out (`videoscreen.hadesdvnotrims`, off): no level 2, 8 or 10 blocks are sent; the picture does not change.
- Dolby Vision TVs whose 12-byte version 1 block says display management 2 or 3 (LG of 2017 and 2018, TCL, Toshiba, Vestel, Vizio, Hisense, Roku, AV receivers) got HDR10, and now get native Dolby Vision (`linux-9978`, and Kodi's rule).
- AMD: native Dolby Vision on DCN 4.0.1 (Radeon RX 9000), refused where the display core would apply anything to the stream on its own (`linux-9973`, `linux-9974`).
- AMD: HDR in YCbCr 4:2:2 at 12 bits where RGB would be 8 bits, on DCN except DCN 3.1 and 3.1.5, which keep 4:2:0 (`linux-9972`).
- AMD: the RGB range on HDMI as CTA-861 says, AMD's change by Adrian Betschart, not yet in a Linux release (`linux-9970`), kept full on a TV that selects the range (`linux-9971`); and the 4K modes whose VIC goes in the HDMI vendor InfoFrame (3840x2160 at 24, 25 and 30 Hz, 4096x2160 at 24 Hz) count as CTA modes too, which AMD's change leaves out (`linux-9983`).
- Intel display version 9 (UHD 6xx): native Dolby Vision no longer needs a connector property that i915 gives only from version 10; on such an HDMI port of 300 MHz it goes out at 2160p24, 25 and 30, and a video at 50 or 60 Hz says that the output has no mode at that rate.
- NVIDIA: the vendor InfoFrame and the HDMI properties on a dual-mode DisplayPort connector while it drives the display as HDMI (`nvidia-open-kernel-modules-9010`, `9011`); HDR at 12 bpc over HDMI on Turing where the mode fits, off by default, with `nvidia-modeset.hades_turing_12bpc=1` on the kernel line (`9012`); and the depth the driver drives the output at, now shown in the player information (`9013`).
- NVIDIA: a repeated picture is drawn again on every refresh, as in 1.0 RC1, since nvidia-drm has no vertical blank to wait for by default; `<video><dvkeepbycommit>true</dvkeepbycommit></video>` (off by default) keeps it another way. An output whose page flips stop coming is turned off and on again after 1, 2, 4 and up to 64 s for as long as they do not come, with its state read from the kernel.
- A native Dolby Vision file opened over another at another frame rate goes straight into its mode: one change of the display where there were up to seven, and none for the next file of a list at the same rate. While a change of mode waits for the video, the menus go out in the mode on the screen instead of freezing.
- The audio engine is told of every modeset of native Dolby Vision; a change of mode that the driver refuses is not taken as made; the HDR state follows what the kernel took; and after a wake from suspend the mode is set again where nvidia-drm left the output off.
- The notice for a display that offers only Dolby Vision Low-Latency is now one sentence that says what to do, shown for the usual time of a notification; the reason is in `kodi.log` and on the Dolby Vision page of the player information.
- Add-ons are told that the display takes Dolby Vision only where native output can be had with the settings as they are, and HDR10+ only where it can go out.
- *System > Display > Monitor* shows each display by the name in its EDID, with its connector, and with its graphics where there are two ("LG TV SSCR2 (HDMI-A-2, NVIDIA)"); a choice saved before keeps working. How a change is made, confirmed and undone is in [the settings](docs/settings.md#kodis-own-settings-that-matter-here).
- 3D: where the display has no 3D mode at the rate of a 30p or 25p video, the 3D mode at exactly twice that rate is used where the display has it, each picture shown twice.

**Files and streams**

- Profile 7 in two video tracks of MP4 files (the dual-track carriage of Dolby's specification) and of Matroska files, also those that libavformat does not group, once their packets show the pair; in a transport stream, also when its enhancement layer is a private stream found to be HEVC while it plays. The log says why two tracks are not joined.
- A HEVC stream whose packets carry an RPU without a Dolby Vision record in the container plays as Dolby Vision (profile 7 with both layers, profile 5, or profile 8 over PQ, HLG or SDR).
- At a play item of a Blu-ray that is not connected seamlessly, about 9 seconds of video were lost; now 1 picture (`ffmpeg-9928`).
- Blu-ray 3D: three fixes to the decoder of both views, for faults that a review of FFmpeg pull request 24303 pointed to.

**Audio**

- PCM goes out in no more channels than the display declares in its EDID (*System > Audio > Limit PCM channels to what the display declares*, Expert, on), also through a change of display mode, which takes the audio data of the display away for a moment.
- Where the display takes only stereo PCM but Dolby in more channels, a multichannel track that Kodi decodes goes out as Dolby Digital Plus 5.1 where the display declares it and the output takes it (192 kHz, as Kodi's own transcoding asks), else as Dolby Digital 5.1, passed through, instead of mixed down to stereo (*Surround as Dolby when the display takes only stereo PCM*, on). Where no encoder opens, stereo PCM as before.
- Kodi's own *Enable Dolby Digital transcoding* counts only where *Number of channels* is 2.0, as in Kodi: left on from a 2.0 setting and hidden at 5.1 or 7.1, it no longer encodes to Dolby there.
- An HDMI or DisplayPort audio output that is set but not there is replaced in memory by the one Kodi would take for it, before its formats are asked, so passthrough works; the setting is not changed, and is used again when that output comes back. What the sink opened is in the log.

**Player information and skins**

- A "GPU" row on the overview page, with the use and temperature that the driver gives; rows on the Dolby Vision page for the video scaling method and for the content mapping sent.
- Every skin gets Hades' overview and Dolby Vision pages: where the skin has no such pages, a second press of the key that opens the player process information opens them.
- cinemaONE's two changes of Kodi pull request 29463: opaque menus stay opaque over HDR video, and a small video window shows the video. One case is further from the SDR reference than before: the edge of a white subtitle without an outline straight over the video.

**Interface**

- The mouse pointer keeps the fraction of a pixel of each motion that libinput gives, which Kodi dropped (1000 reports of 0.4 px moved it 0 px; now 400). Of motions that follow one another in the queue only the last is handled, with buttons, the wheel and keys kept in their place and order, and a drag moves as far as before.
- The server of a stream without a type is asked for it on a thread of its own, as Kodi already waits for a plugin there: the menus keep drawing, with the busy dialog after 100 ms, where the interface froze for several seconds each time a film started from a Debrid add-on. The wait cannot be cancelled, as before.
- Once a minute, when the interface was slow, a line `GUI: in the last N s, ...` in `kodi.log` says how many passes took 50 ms or more with no video playing, how long add-ons held the interface, and how many mouse motions were left for the one after them.

**Diagnostics and logs**

- Every file of the diagnostics goes through the filter before it is written: IP and MAC addresses, Wi-Fi names, serial numbers (also inside EDIDs), UUIDs, SSH fingerprints and the name of the PC become markers made with a secret that never leaves the PC, and the names of media are removed.
- "Submit Log" also filters a log that is uploaded.
- The diagnostics are copied to a USB drive with *System > Submit Log > Copy the diagnostics to a USB drive*, or when a USB drive with a folder `hades-diag` at its top is plugged in; the copy is checked file by file. An update with the installer keeps the folder `hades-diag`.
- The diagnostics service cannot use the network.

**System**

- *System > OS Configuration > Hardware clock*: local time (the default, as before) or UTC.
- *Services > Phone remote control*: turns on what Kore or Yatse need, through Kodi's own confirmations, and shows what to enter in the app.
- *Updates*: "Available Versions" says where the versions are published. A signed update channel is built in and stays off: no image has a channel yet.
- The unused blocks of the storage partition are given back to the drive once a week (`fstrim`, idle priority, only that partition).
- Hardening: kernel options against the exploitation of memory bugs, sysctl settings (no ICMP redirects, the kernel log for root only), BusyBox no longer setuid root, the folders that hold secrets for root only, and compiler hardening on FFmpeg, zlib, sqlite, libjpeg-turbo and taglib, each measured within the noise of playback; not applied where it cost (libass and its fonts, libpng, curl).
- The USB4 and Thunderbolt driver, which LibreELEC lost in 2020; PCIe tunnels stay unauthorised.
- MediaTek MT7925 Wi-Fi and Bluetooth, MT7927 Wi-Fi; button maps for Xbox and DualShock 4 controllers.
- The GBM library of NVIDIA's EGL (egl-gbm, MIT) is built from its source instead of copied from NVIDIA's installer.

**Updates and security**

- Linux 7.2.12, libplacebo 285d1cc8af2a, Kodi Piers up to de8192b4200c, LibreELEC master up to fb1edf29eb07 (systemd 262.1, sqlite 3.54.1, cifs-utils 7.9, Pillow 13.0.0), Python 3.14.9, LLVM 23.1.4, kernel firmware 20261019, elfutils 0.197, ethtool 7.2, NSS 3.131.1, pycryptodome 4.1.0, setuptools 84.2.1, nano 9.3.
- The CA bundle as Mozilla's root store of 2026-10-20: three eMudhra roots are removed, one of which gave a trust path to intermediates whose keys are public (CVE-2026-113812).
- Fixes taken from upstream: FFmpeg (70), OpenSSH (4), Python (3), curl (5), BlueZ (2), libarchive (3), libtiff (3). Fixed by Hades where upstream has no fix yet: libbluray (CVE-2026-112282, CVE-2026-112283), libdisplay-info (CVE-2026-112271), libdvdnav (CVE-2026-112058).

What has been seen on a TV is in the [README](README.md), under "What has been tested". Some ideas came from other builds and were written again for Hades; the README credits them.

### En español

#### Novedades

- **Los menús sobre Dolby Vision cuestan mucho menos.** Con un menú, el OSD, la barra de búsqueda o subtítulos sobre Dolby Vision nativo, cada fotograma se rehace ahora solo donde se dibujan, con los mismos bytes enviados a la tele. En la gráfica integrada del Ryzen 9 7950X3D, la barra de búsqueda sobre una película de perfil 7 tarda 17,8 ms de GPU en vez de 30,5, de los 41,7 ms que tiene cada fotograma (medido con Direct3D 12 en Windows). La medición de lo dibujado sobre las bandas negras, que costaba unos 40 ms por fotograma en una Radeon RX 7600 y hacía que una película con un menú encima fuera a unos 16 fotogramas por segundo (issue #3), se hace ahora por bloques. El perfil 5, cuyos píxeles bajo un menú son los que más cuestan, tiene una pasada propia para ellos.
- **Dolby Vision Low-Latency (player-led), como opción.** Para una entrada de la tele que ofrece Dolby Vision solo en su forma de baja latencia (el modo juego de algunas teles, o una tele que no tiene otra), Hades puede enviarlo ahora así, adaptando él mismo la imagen a la pantalla. Apagado por defecto. La adaptación es propia de Hades, no la de Dolby.
- **HDR10+ a las pantallas que lo admiten.** Los metadatos dinámicos de cada fotograma de un vídeo HDR10+ salen ahora con ese fotograma hacia una pantalla que declara HDR10+, en gráficas NVIDIA, AMD e Intel, dentro de los límites de cada controlador (en los detalles, abajo).
- **Otros formatos como Dolby Vision, como opciones (apagadas por defecto):** un vídeo HDR10+ a una tele que admite Dolby Vision pero no HDR10+, con metadatos hechos de sus propios metadatos HDR10+; y un vídeo HDR10, con metadatos que Hades mide de la imagen mientras se reproduce y que lo dicen: no son los del estudio.
- **Para proyectores:** Dolby Vision se puede enviar sin sus trims, para una pantalla que con ellos muestra negros lavados o colores equivocados (apagado por defecto).
- **HDR y Dolby Vision en una pantalla sin HDR.** HDR10, HLG y los perfiles 5, 7 y 8.1 de Dolby Vision se adaptan a SDR como lo hace una pantalla de referencia (ITU-R BT.2390), que ahora es el método de mapeo de tonos por defecto: medido, mucho más cerca del máster que los métodos propios de Kodi. El perfil 5 en una pantalla así ya no sale verde y morado.
- **Más archivos Dolby Vision se ven como Dolby Vision:** el perfil 7 en dos pistas de vídeo de archivos MKV y MP4 (issues #1 y #5), y los flujos HEVC que llevan Dolby Vision sin un registro de ello en su contenedor.
- **Más teles y gráficas:** teles Dolby Vision que leen la gestión de pantalla 2 o 3 (muchas de 2017 y 2018), las gráficas Radeon RX 9000 (RDNA 4), las gráficas Intel UHD 6xx, y el puerto HDMI de los portátiles cuya gráfica NVIDIA lo maneja a través de un conector DisplayPort de doble modo.
- **Mejor HDR10 en AMD y NVIDIA:** en AMD, HDR10 en YCbCr 4:2:2 a 12 bits donde RGB iría a 8 bits; en las NVIDIA GeForce GTX 16 y RTX 20, HDR por HDMI a 12 bits en vez de 8 donde el modo cabe (apagado por defecto: lo enciende `nvidia-modeset.hades_turing_12bpc=1` en la línea del kernel).
- **Menos fotogramas negros y menos cambios de pantalla:** Dolby Vision mostrado como HDR10 conserva la última imagen entera donde una falla; el vídeo siguiente de una lista a otra frecuencia cambia la pantalla una vez; los menús siguen moviéndose mientras un cambio de pantalla espera; una salida NVIDIA cuya pantalla deja de contestar se apaga y se enciende hasta que vuelve; y al despertar de la suspensión el modo se vuelve a poner donde el controlador de NVIDIA había dejado la salida apagada (un portátil se quedaba sin imagen al despertar).
- **Sonido:** el PCM sale en no más canales de los que la tele dice que admite, también durante un cambio de modo de pantalla (a una tele que declaraba 2 canales de PCM se le podían enviar más, la causa probable de una pista DTS-HD que no sonó; un ajuste Experto lo apaga); donde la tele solo admite PCM estéreo pero Dolby de más canales, una pista multicanal que decodifica Kodi sale como Dolby Digital Plus o Dolby Digital 5.1 en vez de estéreo (un ajuste, activado por defecto); y una salida de audio HDMI configurada que no está ya no deja apagado el paso directo.
- **El ratón y los menús:** el puntero conserva las fracciones de píxel de cada movimiento (un movimiento lento lo movía menos de lo debido, o nada), y los movimientos que se acumulan se atienden como uno; cuando una película empieza desde un complemento, los menús siguen dibujándose, con el indicador de espera, mientras se pregunta al servidor qué es el flujo, en vez de quedarse quietos varios segundos; y `kodi.log` dice una vez por minuto cuándo la interfaz fue lenta.
- **Elegir el monitor:** el ajuste Monitor de *Sistema > Pantalla*, que LibreELEC oculta, vuelve a verse, con el nombre de cada pantalla. Un cambio a una pantalla de la misma gráfica vale al momento; una de otra gráfica reinicia Kodi. En los dos casos Kodi pregunta si mantenerlo y, si no, vuelve atrás. No mientras algo se reproduce.
- **Un diagnóstico que se puede compartir:** las direcciones, los números de serie, los nombres de red y los nombres de las películas pasan a ser marcas o se quitan, en todos los archivos; el diagnóstico se puede copiar a una unidad USB, también sin nada en pantalla, y una actualización con el instalador lo conserva.
- **Más cómodo:** un mando a distancia en el móvil en un paso, un ajuste para un PC cuyo reloj va en UTC, un recorte semanal de la partición de almacenamiento, la información del reproductor de Hades en cualquier skin, con el uso y la temperatura de la GPU, y un endurecimiento que se midió sin coste en la reproducción.
- **Al día y más seguro:** LibreELEC y Kodi en sus últimos commits, libplacebo, LLVM, systemd y el firmware del kernel, y arreglos de fallos de seguridad conocidos en FFmpeg, OpenSSH, Python, curl, BlueZ, libarchive y libtiff, con cuatro arreglos propios de Hades para libbluray, libdisplay-info y libdvdnav.

#### Antes de actualizar

- **Gráficas AMD con una tele que no puede elegir el rango RGB.** Las gráficas AMD envían ahora a esa tele RGB en rango limitado, como ella espera (el cambio es de AMD). Si activaste el ajuste de Kodi de rango de color limitado (16-235), en *Sistema > Pantalla*, porque en esa tele los negros salían aplastados, desactívalo, o el rango se comprime dos veces. Una tele que elige el rango, como la LG C5, recibe lo mismo que antes.
- **Mapeo de tonos.** Un método de mapeo de tonos guardado como Hable, que era el de Kodi por defecto, pasa una vez a "Referencia (BT.2390)". Si después vuelves a elegir Hable, se queda.

#### Detalles

**Imagen y metadatos Dolby Vision**

- La curva PQ de la decodificación Dolby Vision se calcula sin cancelación (`libplacebo-9942`): una imagen de perfil 5 reconstruida como HDR10 tenía bandas de hasta 315 códigos de 4095 en el borde de BT.2020, ahora 39 como mucho.
- La adaptación a una pantalla (SDR, proyectores, Low-Latency) va al pico de la pantalla, no a un pico virtual por encima que oscurecía toda la imagen entre un 2 y un 11 % (`libplacebo-9943`), y su tabla de gama es más fina (96x64x512 en vez de 48x32x256), lo que reduce a la mitad el error más grande en SDR.
- Los menús o subtítulos sobre las bandas negras ya no marcan una escena nueva en los metadatos (ETSI GS CCM 001, 6.2.2): un cambio de la zona activa lleva en su lugar un identificador de metadatos nuevo, y una banda sigue dentro de la zona 2 s de vídeo después de que se vaya lo dibujado encima.
- A una pantalla de gestión de pantalla 2 o 3 se le envía la parte de mapeo de contenido v2.9 de los metadatos, la que lee, sin los bloques de mapeo de contenido v4.0, que no lee.
- Una imagen que sale sin su imagen de mejora (tras un salto, o tarde) lleva el RPU de su propia unidad de acceso, no el de una vecina.
- La capa de mejora arranca por orden de imagen en un punto de acceso aleatorio propio donde las dos capas no comparten ninguno; con la capa base reordenada, antes no arrancaba nunca. Un contexto VAAPI nuevo de la misma pantalla ya no la reinicia, y un flujo MEL cuya capa resulta llevar un residuo lo pierde una vez como mucho, y desde ahí su capa se decodifica.
- Dolby Vision mostrado como HDR10 dibuja en una segunda textura y muestra una imagen solo cuando está entera (132 MB más a 3840x2160); donde no se puede mostrar nada reconstruido, muestra la capa base cuando es una imagen por sí misma. Su copia de 8 bits en rango limitado tiene el blanco en 235, no en 234.
- El método de escalado de vídeo de Kodi no lo usa el motor Dolby Vision, que remuestrea como dice la especificación; la página Dolby Vision de la información del reproductor ahora lo dice.

**Rendimiento**

Los tiempos de esta sección se midieron con Direct3D 12 en Windows, no con los controladores de Linux.

- Un fotograma con elementos de la interfaz sobre Dolby Vision nativo se rehace solo en las teselas de 16x16 píxeles a las que llega la interfaz, con los mismos bytes: en una RTX 4080 SUPER, de 1,4 a 2,7 ms en vez de 1,5 a 9,3. `<video><dvdrawntiles>false</dvdrawntiles></video>` en `advancedsettings.xml` lo apaga, para comparar.
- Lo dibujado sobre las bandas se mide por bloques, leyendo cada píxel una vez: en la gráfica integrada del Ryzen 9 7950X3D, 5,5 ms en vez de 19,6 bajo una pantalla atenuada.
- Fotogramas de perfil 5 con la interfaz encima: los píxeles dibujados encima se convierten en una pasada propia, cada par una vez y con la imagen de debajo decodificada una vez, tres conversiones donde había cuatro, con los mismos bytes: unos 60 ms en vez de unos 83 bajo la información del reproductor en esa gráfica integrada. Donde una GPU no puede hacer la pasada, el transporte convierte él mismo esos píxeles como antes (los mismos bytes, más tiempo de GPU) y `kodi.log` lo dice.
- Donde esos fotogramas de perfil 5 siguen perdiendo refrescos (con *Reducción de fidelidad* en Automático), su conversión sube un nivel cada vez, solo lo necesario: primero una forma más rápida con los mismos bytes, y después un redondeo simplificado que puede dejar un píxel dibujado encima a un código del valor exacto, cosa que entonces muestra la información del reproductor. No cambian los fotogramas sin elementos de la interfaz, los perfiles 7 y 8.1, ni las gráficas que llegan a tiempo.
- La presentación un fotograma por adelantado no la encienden ni la apagan un salto ni unos menús que acaban de aparecer.
- Lo que encuentra la reducción de fidelidad se recuerda ahora también con el transporte por defecto (guarda un nivel para cada juego de niveles que puede tener un formato): una GPU lenta pierde muchos menos refrescos al empezar un vídeo.

**Pantalla y HDMI**

- Dolby Vision Low-Latency (`videoscreen.hadesdvlowlatency`, apagado): solo en una pantalla que ofrece Low-Latency y no Standard. La imagen se adapta con la curva BT.2390, guiada por los metadatos de nivel 1 de cada escena, al pico y a los primarios del bloque Dolby Vision de la pantalla, y se envía como YCbCr 4:2:2 a 12 bits, BT.2020, rango limitado, por el túnel, con el InfoFrame del fabricante de baja latencia (`linux-9975` a `9977` para amdgpu, i915 y xe; `nvidia-open-kernel-modules-9030`). `<video><dvllpattern>true</dvllpattern></video>` muestra un patrón de prueba en lugar de la imagen, para un primer vistazo a una pantalla.
- HDR10+: el InfoFrame del fabricante de cada fotograma sale en el commit de ese fotograma, nunca con un cambio de modo, por una propiedad nueva de DRM (`linux-9980`) con implementaciones para amdgpu, i915, xe y NVIDIA. Menos de 12 imágenes sin metadatos conservan el último InfoFrame; a las 12 vuelve a HDR10. El indicador de gráficos sobre el vídeo se pone mientras la interfaz de Kodi está encima.
- HDR10+ por DisplayPort en NVIDIA Ada (RTX 40): el SDP del InfoFrame se escribe sin la suma de comprobación del hardware, que es la de los InfoFrames HDMI y en un SDP ocuparía el lugar del byte de versión (`nvidia-open-kernel-modules-9020`). La FAQ oficial de HDR10+ Gaming dice que en un PC HDR10+ va solo por HDMI.
- HDR10+ por HDMI en NVIDIA a una pantalla que declara refresco variable por HDMI, donde el controlador ocupa la ranura genérica de InfoFrame con sus propios datos de refresco variable (desde 50 Hz y 720 líneas, se use o no el refresco variable) y HDR10+ se rechazaba: Ada lo envía ahora en la ranura genérica compartida 2, una ranura que NVIDIA solo documenta para DisplayPort; las GTX 16, RTX 20 y RTX 30, que no tienen otra ranura, siguen sin enviarlo. Un rechazo dice por qué en el registro del kernel (`Hades: HDR10+ refused on head N: ...`), y `kodi.log` dice que lo rechazó el controlador.
- HDR10+ por HDMI en Intel a 3840x2160 a 24, 25 y 30 Hz y 4096x2160 a 24 Hz (y sus frecuencias de 1000/1001), donde DRM pone el VIC de HDMI en el InfoFrame del fabricante que HDR10+ también necesita, así que una película a 23,976 Hz salía como HDR10 (issue #2): a una pantalla HDMI 2.0 que lista el VIC CTA del modo y declara HDR10+, el InfoFrame AVI lleva ahora ese VIC y el InfoFrame del fabricante lleva HDR10+ (`linux-9982`). Todo lo demás sale como antes, y un rechazo dice por qué en el registro del kernel.
- HDR10+ como Dolby Vision (`videoscreen.hadesdvfromhdr10plus`, apagado): metadatos de perfil 8.1 hechos escena a escena a partir de los metadatos HDR10+, con las escenas como las cuenta hdr10plus_tool y los valores por defecto de dovi_tool; la media y el pico de cada escena se toman con reglas propias de Hades ([cómo funciona](docs/how-it-works.md#other-formats-sent-as-dolby-vision), en inglés).
- HDR10 como Dolby Vision, medido (`videoscreen.hadesdvfromhdr10`, apagado): el nivel 1 y la zona activa medidos en la GPU sobre las imágenes que Kodi ya ha decodificado por delante, y los cortes de escena encontrados en la imagen y marcados como tales (se pierden unos pocos, y otros pocos se encuentran donde no los hay); sin trims ni nivel 3.
- Sin trims (`videoscreen.hadesdvnotrims`, apagado): no se envían bloques de nivel 2, 8 ni 10; la imagen no cambia.
- Teles Dolby Vision cuyo bloque de versión 1 de 12 bytes dice gestión de pantalla 2 o 3 (LG de 2017 y 2018, TCL, Toshiba, Vestel, Vizio, Hisense, Roku, receptores AV) recibían HDR10, y ahora reciben Dolby Vision nativo (`linux-9978`, y la regla de Kodi).
- AMD: Dolby Vision nativo en DCN 4.0.1 (Radeon RX 9000), rechazado donde el núcleo de pantalla aplicaría algo al flujo por su cuenta (`linux-9973`, `linux-9974`).
- AMD: HDR en YCbCr 4:2:2 a 12 bits donde RGB iría a 8 bits, en DCN salvo DCN 3.1 y 3.1.5, que siguen en 4:2:0 (`linux-9972`).
- AMD: el rango RGB por HDMI como dice CTA-861, el cambio de AMD de Adrian Betschart, que aún no está en ninguna versión de Linux (`linux-9970`), con rango completo en una tele que elige el rango (`linux-9971`); y los modos 4K cuyo VIC va en el InfoFrame del fabricante de HDMI (3840x2160 a 24, 25 y 30 Hz, 4096x2160 a 24 Hz) cuentan también como modos CTA, que el cambio de AMD deja fuera (`linux-9983`).
- Intel de versión de pantalla 9 (UHD 6xx): el Dolby Vision nativo ya no necesita una propiedad del conector que i915 solo da desde la versión 10; en un puerto HDMI así de 300 MHz sale a 2160p24, 25 y 30, y un vídeo a 50 o 60 Hz dice que la salida no tiene modo a esa frecuencia.
- NVIDIA: el InfoFrame del fabricante y las propiedades HDMI en un conector DisplayPort de doble modo mientras maneja la pantalla como HDMI (`nvidia-open-kernel-modules-9010`, `9011`); HDR a 12 bpc por HDMI en Turing donde el modo cabe, apagado por defecto, con `nvidia-modeset.hades_turing_12bpc=1` en la línea del kernel (`9012`); y la profundidad a la que el controlador maneja la salida, que ahora muestra la información del reproductor (`9013`).
- NVIDIA: una imagen repetida se vuelve a dibujar en cada refresco, como en 1.0 RC1, porque nvidia-drm no tiene por defecto borrado vertical que esperar; `<video><dvkeepbycommit>true</dvkeepbycommit></video>` (apagado por defecto) la conserva de otra forma. Una salida cuyos cambios de página dejan de llegar se apaga y se enciende tras 1, 2, 4 y hasta 64 s mientras no lleguen, con su estado leído del kernel.
- Un archivo de Dolby Vision nativo abierto sobre otro a otra frecuencia pasa directamente a su modo: un cambio de pantalla donde había hasta siete, y ninguno para el archivo siguiente de una lista a la misma frecuencia. Mientras un cambio de modo espera al vídeo, los menús salen en el modo que hay en pantalla en vez de quedarse quietos.
- Al motor de audio se le avisa de cada cambio de modo del Dolby Vision nativo; un cambio de modo que el controlador rechaza no se da por hecho; el estado HDR sigue a lo que tomó el kernel; y al despertar de la suspensión el modo se vuelve a poner donde nvidia-drm dejó la salida apagada.
- El aviso para una pantalla que solo ofrece Dolby Vision Low-Latency es ahora de una frase que dice qué hacer, y se muestra el tiempo habitual de un aviso; el motivo está en `kodi.log` y en la página Dolby Vision de la información del reproductor.
- A los complementos se les dice que la pantalla admite Dolby Vision solo donde se puede tener la salida nativa con los ajustes tal como están, y HDR10+ solo donde puede salir.
- El ajuste Monitor de *Sistema > Pantalla* muestra cada pantalla por el nombre de su EDID, con su conector, y con su gráfica donde hay dos ("LG TV SSCR2 (HDMI-A-2, NVIDIA)"); una elección guardada antes sigue valiendo. Cómo se hace, se confirma y se deshace un cambio está en [los ajustes](docs/settings.md#kodis-own-settings-that-matter-here), en inglés.
- 3D: donde la pantalla no tiene modo 3D a la frecuencia de un vídeo de 30p o 25p, se usa el modo 3D a exactamente el doble donde la pantalla lo tiene, y cada imagen se muestra dos veces.

**Archivos y flujos**

- El perfil 7 en dos pistas de vídeo de archivos MP4 (el transporte en dos pistas de la especificación de Dolby) y de archivos Matroska, también los que libavformat no agrupa, en cuanto sus paquetes muestran la pareja; en un flujo de transporte, también cuando su capa de mejora es un flujo privado que resulta ser HEVC mientras se reproduce. El registro dice por qué no se unen dos pistas.
- Un flujo HEVC cuyos paquetes llevan un RPU sin un registro Dolby Vision en el contenedor se reproduce como Dolby Vision (perfil 7 con sus dos capas, perfil 5, o perfil 8 sobre PQ, HLG o SDR).
- En un elemento de reproducción de un Blu-ray que no está unido sin cortes se perdían unos 9 segundos de vídeo; ahora 1 imagen (`ffmpeg-9928`).
- Blu-ray 3D: tres arreglos al decodificador de las dos vistas, de fallos que señaló una revisión de la pull request 24303 de FFmpeg.

**Audio**

- El PCM sale en no más canales de los que la pantalla declara en su EDID (*Sistema > Audio > Limitar los canales PCM a lo que declara la pantalla*, Experto, activado), también durante un cambio de modo de pantalla, que se lleva un momento los datos de audio de la pantalla.
- Donde la pantalla solo admite PCM estéreo pero Dolby de más canales, una pista multicanal que decodifica Kodi sale como Dolby Digital Plus 5.1 donde la pantalla lo declara y la salida lo admite (192 kHz, como pide la transcodificación propia de Kodi), si no como Dolby Digital 5.1, en paso directo, en vez de mezclarse a estéreo (*Envolvente como Dolby cuando la pantalla solo admite PCM estéreo*, activado). Donde no abre ningún codificador, PCM estéreo como antes.
- El ajuste propio de Kodi que transcodifica a Dolby Digital solo cuenta donde el número de canales es 2.0, como en Kodi: dejado activado desde un ajuste a 2.0 y oculto a 5.1 o 7.1, ya no codifica a Dolby ahí.
- Una salida de audio HDMI o DisplayPort configurada que no está se sustituye en memoria por la que Kodi tomaría en su lugar, antes de preguntar sus formatos, así que el paso directo funciona; el ajuste no se cambia, y se vuelve a usar cuando esa salida vuelve. Lo que abre la salida de sonido queda en el registro.

**Información del reproductor y skins**

- Una fila "GPU" en la página de resumen, con el uso y la temperatura que da el controlador; filas en la página Dolby Vision para el método de escalado de vídeo y para el mapeo de contenido enviado.
- Todas las skins reciben las páginas de resumen y Dolby Vision de Hades: donde la skin no tiene esas páginas, una segunda pulsación de la tecla que abre la información del proceso del reproductor las abre.
- Los dos cambios de cinemaONE de la pull request 29463 de Kodi: los menús opacos siguen opacos sobre vídeo HDR, y una ventana de vídeo pequeña muestra el vídeo. Un caso queda más lejos de la referencia SDR que antes: el borde de un subtítulo blanco sin contorno directamente sobre el vídeo.

**Interfaz**

- El puntero del ratón conserva la fracción de píxel de cada movimiento que da libinput, que Kodi tiraba (1000 informes de 0,4 px lo movían 0 px; ahora 400). De los movimientos que se siguen en la cola solo se atiende el último, con los botones, la rueda y las teclas en su sitio y en su orden, y un arrastre llega tan lejos como antes.
- Al servidor de un flujo sin tipo se le pregunta el tipo en un hilo propio, como Kodi ya espera ahí a un plugin: los menús siguen dibujándose, con el indicador de espera a los 100 ms, donde la interfaz se quedaba quieta varios segundos cada vez que una película empezaba desde un complemento Debrid. La espera no se puede cancelar, como antes.
- Una vez por minuto, cuando la interfaz fue lenta, una línea `GUI: in the last N s, ...` en `kodi.log` dice cuántas pasadas tardaron 50 ms o más sin vídeo, cuánto tiempo tuvieron los complementos la interfaz, y cuántos movimientos del ratón se dejaron para el siguiente.

**Diagnóstico y registros**

- Cada archivo del diagnóstico pasa por el filtro antes de escribirse: las direcciones IP y MAC, los nombres de las redes Wi-Fi, los números de serie (también dentro de los EDID), los UUID, las huellas SSH y el nombre del PC pasan a ser marcas hechas con un secreto que nunca sale del PC, y los nombres de los medios se quitan.
- "Enviar registro" también filtra el registro que se sube.
- El diagnóstico se copia a una unidad USB con *Sistema > Enviar registro > Copiar el diagnóstico a una unidad USB*, o al enchufar una unidad USB con una carpeta `hades-diag` en su raíz; la copia se comprueba archivo por archivo. Una actualización con el instalador conserva la carpeta `hades-diag`.
- El servicio del diagnóstico no puede usar la red.

**Sistema**

- *Sistema > Identificación > Reloj de la placa*: hora local (por defecto, como antes) o UTC.
- *Servicios > Control remoto desde el móvil*: activa lo que necesitan Kore o Yatse, con las confirmaciones del propio Kodi, y muestra lo que hay que poner en la aplicación.
- *Actualizaciones*: "Versiones disponibles" dice dónde se publican las versiones. Un canal de actualizaciones firmado viene incorporado y sigue apagado: ninguna imagen tiene todavía un canal.
- Los bloques sin usar de la partición de almacenamiento se devuelven a la unidad una vez por semana (`fstrim`, con prioridad mínima, solo esa partición).
- Endurecimiento: opciones del kernel contra el aprovechamiento de fallos de memoria, ajustes de sysctl (sin redirecciones ICMP, el registro del kernel solo para root), BusyBox ya no es setuid root, las carpetas que guardan secretos solo para root, y endurecimiento del compilador en FFmpeg, zlib, sqlite, libjpeg-turbo y taglib, cada uno medido dentro del ruido de la reproducción; no se aplica donde costaba (libass y sus fuentes, libpng, curl).
- El controlador de USB4 y Thunderbolt, que LibreELEC perdió en 2020; los túneles PCIe siguen sin autorizar.
- Wi-Fi y Bluetooth MediaTek MT7925, Wi-Fi MT7927; mapas de botones de los mandos Xbox y DualShock 4.
- La biblioteca GBM del EGL de NVIDIA (egl-gbm, MIT) se compila desde su código fuente en vez de copiarse del instalador de NVIDIA.

**Actualizaciones y seguridad**

- Linux 7.2.12, libplacebo 285d1cc8af2a, Kodi Piers hasta de8192b4200c, LibreELEC master hasta fb1edf29eb07 (systemd 262.1, sqlite 3.54.1, cifs-utils 7.9, Pillow 13.0.0), Python 3.14.9, LLVM 23.1.4, firmware del kernel 20261019, elfutils 0.197, ethtool 7.2, NSS 3.131.1, pycryptodome 4.1.0, setuptools 84.2.1, nano 9.3.
- Los certificados raíz como el almacén de Mozilla del 2026-10-20: se quitan tres raíces de eMudhra, una de las cuales daba una cadena de confianza a intermedias cuyas claves son públicas (CVE-2026-113812).
- Arreglos tomados de los proyectos originales: FFmpeg (70), OpenSSH (4), Python (3), curl (5), BlueZ (2), libarchive (3), libtiff (3). Arreglados por Hades donde el proyecto original aún no tiene arreglo: libbluray (CVE-2026-112282, CVE-2026-112283), libdisplay-info (CVE-2026-112271), libdvdnav (CVE-2026-112058).

Lo que se ha visto en una tele está en el [README en español](README.es.md), en "Qué se ha probado". Algunas ideas vinieron de otras compilaciones y se escribieron de nuevo para Hades; el README las acredita.

## 1.0 RC2

### What's new for you

- **Native Dolby Vision rebuilt as the specification defines it.** The picture sent to the TV is now rebuilt exactly as the published Dolby Vision specification describes it, and sent the way certified players send it. Colours, edges and bright highlights are more faithful in every profile (5, 7 FEL, 7 MEL and 8.1).
- **Smoother on modest graphics.** Less GPU work on every frame, and a repeated picture is not drawn again. Only on graphics that cannot keep up does the picture lose a small, measured amount of precision, and only as much as needed; graphics that keep up stay bit-exact.
- **Menus at 120 Hz.** Starting and stopping Dolby Vision, HDR10 and HLG is a single display change on NVIDIA, AMD and Intel, also with the menus at 119.88 or 120 Hz.
- **Turn the TV on after the PC.** The TV's EDID is read again when each video starts, so Dolby Vision works even if the PC started with the TV off, and a later change (Deep Colour turned on, another TV) is seen.
- **New "Overview" page** in the player information: what is sent to the TV (signal, mode, colour depth, the metadata sent), the video, the audio and the system.
- **Projectors and TVs without Dolby Vision:** Dolby Vision shown as HDR10 can be mapped to the brightness of the display (off by default), and the Dolby Vision output can be chosen for each video.
- **Older AMD graphics:** native Dolby Vision also on Raven, Picasso, Navi 1x, Renoir and Cezanne (DCN 1.0 and 2.x).
- **HDR10 at 10 bits** on Intel and AMD where RGB would be 8 bits, in YCbCr 4:2:0, with the BT.2020 YCbCr matrix on Intel.
- **Live TV:** audio passthrough (Atmos included) on live channels and streams (a setting, on by default), a picture on 10-bit channels that showed none, and faster channel changes.
- **Profile 7 MEL** no longer decodes its empty enhancement layer: less work, the same picture, byte for byte. A FEL is always decoded.
- **Up to date and safer:** Linux 7.2.11, Kodi and LibreELEC at their latest commits, and fixes for known security issues in glibc, FFmpeg, curl, libnfs, libass and more.

### Details

**Dolby Vision picture**

- The reconstruction follows the normative composer of ETSI GS CCM 001: identical bit for bit to an integer reference of the specification in every case tested. libplacebo's reshaping, which departs from the specification by 4 to 15 codes on most pixels (and by hundreds at strong colour edges), stays available as an expert setting.
- Sent the way a certified source device sends it: the composer's output in the colour representation of its RPU (IPT for profile 5), with the RPU's own matrices in the display management metadata; menus and subtitles are blended into it, and pixels they do not touch are sent exactly as composed. An expert setting sends it converted to BT.2020, as 1.0 RC1 did.
- Chroma of both layers at the position the stream signals; 4:2:2 taken from the even pixel; exact Lanczos weights for whole-ratio scaling; very bright pixels that came out black are fixed; exact BT.2020, P3 and BT.709 matrices.
- Level 5 describes what is sent, also when menus or subtitles are drawn over the bars; metadata up to 512 bytes; level 11 checked as dovi_tool checks it; scene refresh after a gap.

**Performance**

- Automatic fidelity step-down only where frames are missed (measured per format, remembered per GPU and driver); it can be turned off.
- Repeated pictures are not drawn again; shader cache on disk; GPU programs prepared before the first frame; fewer imports per frame.
- A switch to check on the GPU that frames send the bytes they claim (`<dvverify>` in `advancedsettings.xml`).

**Display and settings**

- A frame whose page flip never comes no longer stalls playback.
- Where the graphics driver refuses native Dolby Vision (Intel and AMD), `kodi.log` and the Dolby Vision page of the player information give its reason.
- Dolby Vision engine settings, Automatic by default.
- DRM PRIME hands back what it cannot decode.

**Updates and security**

- Linux 7.2.11, libplacebo 804dc71cd38d, Kodi Piers up to 01d4146d5d25, LibreELEC master up to 3ca0d49bee40.
- Fixes taken from upstream: glibc 2.44 (nine CVEs), FFmpeg (about ninety), libnfs (CVE-2026-108985), libass (CVE-2026-95127), curl, libbluray, libdvdread, libdvdnav, libarchive, Samba, BusyBox; BlueZ CVE-2026-105460 fixed by Hades.

What has been seen on a TV is in the [README](README.md), under "What has been tested". Some ideas came from other Dolby Vision builds and were written again for Hades; the README credits them.

### En español

#### Novedades

- **Dolby Vision nativo reconstruido como lo define la especificación.** La imagen que se envía a la tele se reconstruye ahora exactamente como la describe la especificación publicada de Dolby Vision, y se envía como la envían los reproductores certificados. Colores, bordes y altas luces más fieles en todos los perfiles (5, 7 FEL, 7 MEL y 8.1).
- **Más fluido en gráficas modestas.** Menos trabajo de GPU en cada fotograma, y una imagen repetida no se vuelve a dibujar. Solo en las gráficas que no llegan a tiempo pierde la imagen una cantidad pequeña y medida de precisión, y solo la necesaria; las gráficas que llegan siguen exactas bit a bit.
- **Menús a 120 Hz.** Empezar y terminar Dolby Vision, HDR10 y HLG es un solo cambio de pantalla en NVIDIA, AMD e Intel, también con los menús a 119,88 o 120 Hz.
- **Encender la tele después del PC.** El EDID de la tele se vuelve a leer al empezar cada vídeo, así que Dolby Vision funciona aunque el PC arrancara con la tele apagada, y se ve un cambio posterior (Deep Colour activado, otra tele).
- **Nueva página "Resumen"** en la información del reproductor: lo que se envía a la tele (señal, modo, profundidad de color, los metadatos enviados), el vídeo, el audio y el sistema.
- **Proyectores y teles sin Dolby Vision:** Dolby Vision mostrado como HDR10 se puede adaptar al brillo de la pantalla (apagado por defecto), y la salida Dolby Vision se puede elegir para cada vídeo.
- **Gráficas AMD más antiguas:** Dolby Vision nativo también en Raven, Picasso, Navi 1x, Renoir y Cezanne (DCN 1.0 y 2.x).
- **HDR10 a 10 bits** en Intel y AMD donde RGB iría a 8 bits, en YCbCr 4:2:0, con la matriz YCbCr BT.2020 en Intel.
- **TV en directo:** paso directo del audio (Atmos incluido) en los canales y flujos en directo (un ajuste, activado por defecto), imagen en los canales de 10 bits que no la mostraban, y cambios de canal más rápidos.
- **El perfil 7 MEL** ya no decodifica su capa de mejora vacía: menos trabajo, la misma imagen, byte a byte. Un FEL siempre se decodifica.
- **Al día y más seguro:** Linux 7.2.11, Kodi y LibreELEC en sus últimos commits, y arreglos de fallos de seguridad conocidos en glibc, FFmpeg, curl, libnfs, libass y otros.

#### Detalles

**Imagen Dolby Vision**

- La reconstrucción sigue el compositor normativo de ETSI GS CCM 001: idéntica bit a bit a una referencia entera de la especificación en todos los casos probados. El reshaping de libplacebo, que se aparta de la especificación de 4 a 15 códigos en la mayoría de los píxeles (y en cientos en los bordes de color fuertes), sigue disponible como ajuste experto.
- Se envía como lo envía un dispositivo de origen certificado: la salida del compositor en la representación de color de su RPU (IPT para el perfil 5), con las propias matrices del RPU en los metadatos de gestión de pantalla; los menús y subtítulos se mezclan en ella, y los píxeles que no tocan se envían exactamente como se compusieron. Un ajuste experto la envía convertida a BT.2020, como hacía 1.0 RC1.
- Croma de las dos capas en la posición que señala el flujo; 4:2:2 tomado del píxel par; pesos Lanczos exactos para escalados de proporción entera; arreglados los píxeles muy brillantes que salían negros; matrices BT.2020, P3 y BT.709 exactas.
- El nivel 5 describe lo que se envía, también cuando hay menús o subtítulos dibujados sobre las bandas; metadatos de hasta 512 bytes; el nivel 11 se comprueba como lo comprueba dovi_tool; renovación de escena tras un hueco.

**Rendimiento**

- Reducción automática de fidelidad solo donde se pierden fotogramas (medida por formato, recordada por GPU y controlador); se puede apagar.
- Las imágenes repetidas no se vuelven a dibujar; caché de sombreadores en disco; programas de GPU preparados antes del primer fotograma; menos importaciones por fotograma.
- Un interruptor para comprobar en la GPU que los fotogramas envían los bytes que dicen (`<dvverify>` en `advancedsettings.xml`).

**Pantalla y ajustes**

- Un fotograma cuyo cambio de página nunca llega ya no detiene la reproducción.
- Donde el controlador de la gráfica rechaza el Dolby Vision nativo (Intel y AMD), `kodi.log` y la página Dolby Vision de la información del reproductor dan su motivo.
- Ajustes del motor Dolby Vision, en Automático por defecto.
- DRM PRIME devuelve lo que no puede decodificar.

**Actualizaciones y seguridad**

- Linux 7.2.11, libplacebo 804dc71cd38d, Kodi Piers hasta 01d4146d5d25, LibreELEC master hasta 3ca0d49bee40.
- Arreglos tomados de los proyectos originales: glibc 2.44 (nueve CVE), FFmpeg (unos noventa), libnfs (CVE-2026-108985), libass (CVE-2026-95127), curl, libbluray, libdvdread, libdvdnav, libarchive, Samba, BusyBox; el CVE-2026-105460 de BlueZ, arreglado por Hades.

Lo que se ha visto en una tele está en el [README en español](README.es.md), en "Qué se ha probado". Algunas ideas vinieron de otras compilaciones con Dolby Vision y se escribieron de nuevo para Hades; el README las acredita.

## 1.0 RC1

The first public release. What it added to LibreELEC, its known limits and the versions of its main parts are in [its README](https://github.com/hadesdevls/LibreELEC-Hades/blob/v1.0-rc1/README.md).

*La primera versión pública. Qué añadía a LibreELEC, sus límites conocidos y las versiones de sus partes principales están en [su README en español](https://github.com/hadesdevls/LibreELEC-Hades/blob/v1.0-rc1/README.es.md).*

---

The changes of LibreELEC itself are in [its own history](https://github.com/LibreELEC/LibreELEC.tv/commits/master); each version of this build is made on one of its commits, which is in the history of this repository.
