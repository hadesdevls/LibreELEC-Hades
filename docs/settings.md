# Settings

The settings that LibreELEC Hades adds to Kodi. Kodi shows them when the settings level is **Advanced** or **Expert** (the level is changed at the bottom of the list on the left of a settings screen). Some appear only where they can apply, as said below. Each one says on screen what it does; this page puts them in one place.

## System > Display > HDR

| Setting | Default | What it does |
| --- | --- | --- |
| **GUI and subtitle brightness in HDR** | 100 % | Brightness of the menus, the OSD and text subtitles while the TV is in HDR10, HLG or Dolby Vision mode, relative to the BT.2408 reference white (100 % = 203 nits, as in Kodi). From 25 to 300 %. Image subtitles made in HDR (UHD Blu-ray) keep their own levels. The video is never changed. Shown only with an HDR display. |
| **Dolby Vision as HDR10 when needed** | On | When Dolby Vision cannot be sent as such (a display without it, or a TV input in its game mode), a Dolby Vision video whose base picture is not HDR10 (profile 5, as streaming services use it) is rebuilt and sent as HDR10; without it, such a video plays with wrong colours. Other Dolby Vision videos play their HDR10 picture. Turn it off if such a video shows a black or broken picture. Applies from the next video. Shown only with an HDR display. See [how-it-works.md](how-it-works.md#dolby-vision-shown-as-hdr10). |
| **Also with an enhancement layer** | Off | Under the one above: also rebuilds Dolby Vision videos with an enhancement layer (profile 7, from UHD Blu-ray) and sends them as HDR10, instead of playing their HDR10 picture. What is sent is the Dolby Vision master, which the display adapts as plain HDR10: it is not always better than the disc's HDR10 version, and it asks more of the graphics. |
| **Dolby Vision: a frame ahead when needed** | On | Where the graphics need most of the time of a frame to prepare a Dolby Vision picture, the next frame is prepared while the last one waits to be shown, so that a frame that takes a little longer is still shown in time. The menus over such a video react one frame later. It is done only where it is needed. Turn it off if native Dolby Vision video stutters or shows a broken picture with it. Applies from the next video. See [how-it-works.md](how-it-works.md#a-frame-ahead-when-needed). |
| **10-bit video at full depth (NVIDIA)** | On | NVIDIA graphics cards give no 10-bit output, only an 8-bit and a 16-bit floating-point one; with this on, HDR10, HLG and other 10-bit video go out through the 16-bit one, without the banding of 8 bits. Turn it off if such video shows wrong colours or a black screen. Dolby Vision is not affected. Applies from the next video. Shown only at the Expert level, where the graphics have such an output (NVIDIA). |

## System > Display > Stereoscopic 3D

| Setting | Default | What it does |
| --- | --- | --- |
| **Switch the display to 3D (HDMI)** | Automatic | For a 3D TV or projector. While a split 3D picture is drawn (over/under or side by side), the display is told so over HDMI and switches to 3D by itself; with frame packing, which keeps both eyes at full resolution, where the display and the graphics driver have it. Off: the two halves go out as an ordinary picture and the display is switched to 3D by hand. Turn it off if a 3D video shows a black or broken picture. |

## Player > Videos > Stereoscopic 3D

| Setting | Default | What it does |
| --- | --- | --- |
| **Decode both views of 3D Blu-ray video (MVC)** | Automatic | Automatic: both views are decoded, by the processor, where the display says that it takes 3D (it has to be on when Kodi starts); elsewhere the video plays in 2D as before, with hardware decoding. Always: on any display, for anaglyph glasses or a 3D display that does not say what it is. Off: never. Applies from the next video. |

## Kodi's own settings that matter here

- **Player > Videos > Adjust display refresh rate.** Dolby Vision follows it, and never changes the refresh rate it gives. With it on, a video gets its own refresh rate. With it off, the refresh rate stays the one of the menus: native Dolby Vision is then used if that rate can carry it (up to 60 Hz), and with the menus at 1920x1080 the video's mode is 3840x2160 at that rate. With the menus at 100 Hz or more and the adjustment off, a Dolby Vision video plays as HDR10.

## Hidden settings and `advancedsettings.xml`

| Where | What | Default |
| --- | --- | --- |
| `videoscreen.hadesdvpath` in `guisettings.xml` (not shown in the settings screens) | The way of drawing found for each graphics chip, driver and build ([how-it-works.md](how-it-works.md#two-ways-of-drawing-a-frame)). Empty it to have it measured again by the next Dolby Vision video | Empty |
| `<video><dvaheadalways>true</dvaheadalways></video>` in `advancedsettings.xml` | Presents Dolby Vision frames a frame ahead on any graphics, not only where it is needed | `false` |
| `<video><dvearlymask>false</dvearlymask></video>` in `advancedsettings.xml` | Stops writing the black bars before the picture is rebuilt. It changes no pixel; it is there to measure what it saves | `true` |
