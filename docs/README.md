# Documentation

More detail than the [README](../README.md) has, for those who want to know how LibreELEC Hades works, what each of its settings does, or what to send with a report. In English; the README is also [in Spanish](../README.es.md).

| Document | What it covers |
| --- | --- |
| [How the Dolby Vision output works](how-it-works.md) | The path of a frame from the file to the HDMI cable, the metadata that is sent, when native Dolby Vision is used and when it is not, the two ways of drawing a frame, the frame-ahead presentation, the step-down in fidelity where the graphics chip cannot keep up, leaving Dolby Vision, Dolby Vision shown as HDR10, Dolby Vision Low-Latency, HDR10+, other formats sent as Dolby Vision (HDR10+, HDR10, and Dolby Vision without its trims), a display without HDR, files and streams (two video tracks, streams without a Dolby Vision record), other parts, the backend of each graphics driver, and where each part is in the source |
| [Settings](settings.md) | Every setting this build adds to Kodi and to the LibreELEC settings add-on, where it is, its key, its default and what it does; what is changed outside the settings screens; Kodi's own settings that matter here; hidden settings and `advancedsettings.xml` |
| [Diagnostics and logs](diagnostics.md) | The diagnostic switch and what it keeps, how to copy it to a USB drive, what the filter takes out and what it leaves, Submit Log, and the lines of `kodi.log` that tell most |

What has been tested, the known limits, the defaults that differ from LibreELEC, how to install, update and build, and the credits are in the [README](../README.md).
