# Known limits

What the connection, the graphics and the display allow, and how to get the most from them. Most of these come from the untouched HDMI signal that native Dolby Vision needs, or from what a graphics driver can send. Where a video cannot go out the way you chose, it plays the ordinary way, and the first page of the player information says why in one sentence.

## The connection

- **Native Dolby Vision needs an untouched HDMI signal.** Variable refresh rate, scaling and colour adjustments in the graphics driver are not compatible with it. It goes out:
  - from an HDMI port of the graphics;
  - through a passive DisplayPort adaptor (DP++) that identifies itself as HDMI: a type 2 adaptor carries it up to 3840x2160 at 30 Hz; a type 1 adaptor stops at 1080p, below the 3840x2160 that Dolby Vision goes out at;
  - on NVIDIA, also from a DisplayPort port that the graphics drive as HDMI, as the HDMI port of some laptops;
  - on Intel graphics, also through the DisplayPort-to-HDMI converter of a board on the list, or of a USB-C hub with the expert setting.

  Most other active converters between DisplayPort or USB-C and HDMI do not carry it, and an adaptor that does not identify itself as HDMI gets no Dolby Vision. Where Dolby Vision cannot go out natively, the video plays the ordinary way, and the player information says why.
- **Native Dolby Vision goes out at 3840x2160, up to 60 Hz.** On Intel UHD 6xx graphics, up to 30 Hz and only from an HDMI port of the graphics itself.
- **HDR in 4:2:2 at 12 bits over HDMI 2.0** (Intel and AMD graphics) needs one more change of mode at the start of an HDR video at 3840x2160 and 50 or 60 Hz, so the TV blanks once more. If that bothers you, turn the setting off in *System > Display*, and HDR goes out as before.

## Graphics

- **Radeon RX 400 and 500 (Polaris) and Vega cards** play Dolby Vision as HDR10 unless you turn on *System > Display > Dolby Vision on this Radeon (experimental)* (Expert; it appears only on those cards). An RX 550 with an LG G3 showed a black picture with the Dolby Vision logo, and was found changing the pixels; where *Check the Dolby Vision output* (in *Check this setup*) finds a card not exact, Hades does not use the path on that output.
- **GeForce GTX 900 and GTX 10 cards and the TITAN V** use NVIDIA's driver 580, which Hades chooses at start by the card. A PC with one of them and a newer NVIDIA card at once uses the newer driver, and the older card gets none. Over HDMI, driver 580 sends RGB at 8 bits at most on these cards: on the GTX 10 cards and the TITAN V, Hades sends HDR as YCbCr 4:2:2 at 10 bits instead where the TV takes it (*HDR at more than 8 bits where the card sends RGB at 8 (NVIDIA)*, on by default), and the GTX 900 cards stay at 8 bits over HDMI. DisplayPort is not under that limit.
- **AMD integrated graphics:** the first Dolby Vision video after a new installation may stutter briefly while Hades measures which of two ways of drawing is faster there. Updates keep what it found, from the update after 1.2 on.
- **Integrated graphics and profile 5:** if a profile 5 video drops frames while the player information is open, close it. Since 1.2 the page is drawn at most four times a second, which costs the graphics less.
- **Video at its own rate with VRR** is exact where the display engine holds each picture until its time (NVIDIA) or keeps the rate itself (Media VRR, RTX 50 series); on AMD graphics Kodi times each picture, and `kodi.log` says how close it came. Native Dolby Vision and HLG always change the mode, and so do HDR10+ and Dolby Vision sent as HDR10, HDR10+ or SDR, except on the RTX 40 and RTX 50 series.

## What Hades makes itself

- **Dolby Vision Low-Latency is mapped by Hades,** not by Dolby's display management, which is not public. The TV is in its Dolby Vision mode and shows Hades' mapping to the brightness and colours that its Dolby Vision block gives.
- **HDR10 sent as Dolby Vision** carries metadata measured from the picture as it plays, not the studio's: scene cuts are found from the picture, and a few are missed or found where there is none. **HDR10+ sent as Dolby Vision** keeps the brightness of each scene, not the studio's tone curve, target display or windows (except in Low-Latency, where Hades maps the picture itself).
- **HDR10+ goes out where the graphics driver can send it with each frame,** otherwise as HDR10; [How it works](how-it-works.md#hdr10) says where. The player information says "HDR10+" while its metadata go out.
- **A 3D LUT corrects the video, not the menus and subtitles,** and it is not applied where the display maps the picture itself: native Dolby Vision, and HDR that the display maps.
- **Filmmaker Mode by itself** follows a change of display mode at the start of a video. Where the video plays in the mode of the menus (at the same refresh rate, with *Adjust display refresh rate* off, or *Video at its own rate with VRR* at *Always*), the TV keeps the content type of the menus.

This is an independent implementation written from public specifications. It has not been tested or certified by Dolby or by HDR10+ Technologies, and it may differ from a certified player.

## 3D and discs

- **3D:** the 3D results in [What has been tested](tested.md) come from users with 3D displays. If a 3D video shows a black or broken picture, turn off *System > Display > Switch the display to 3D (HDMI)*. Through an active DisplayPort or USB-C converter the graphics driver offers no 3D mode, and `kodi.log` says so. A Dolby Vision 3D film (profile 20) plays in 3D with each view rebuilt as HDR10 or SDR, since no display takes Dolby Vision in 3D. More in [How it works](how-it-works.md#stereoscopic-3d).
- **BD-Live** reaches the Internet over IPv4 only. The updates that a disc can fetch over BD-Live are not implemented by libbluray, so such a disc is told that the update failed.

## The PC

- **With Secure Boot,** only a drive written with the installer's *Start with Secure Boot* starts.
- **Secure Boot does not protect against software running as root in Hades,** which today includes every add-on.
- **The USB receiver of a wireless keyboard, mouse or remote** (2.4 GHz) can lose range next to USB 3 ports, cables and hubs, whose links give off noise in that band. A USB 2.0 port fixed it for a user (issue #13).
