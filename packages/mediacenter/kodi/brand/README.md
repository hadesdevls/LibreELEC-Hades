# The images under which this build shows itself

This build is a modified build of Kodi that the XBMC Foundation has not approved, and an unofficial
build of LibreELEC. Kodi's trademark policy asks such a build not to use Kodi's name and logos as
its own, and it would be wrong for it to show LibreELEC's logo as if it were LibreELEC's. So the
pictures that carried them are replaced by the ones made here. Kodi is a registered trademark of
the XBMC Foundation.

The mark is an eclipse at the instant of its first light: a dark disc, the thin rim of light around
it, and the one point where the light breaks through, with the thin line that a camera lens draws
from a light so much brighter than the rest. It is the picture of what this build is for, light at
its brightest next to black at its deepest; and of its name, which has been read since antiquity as
"the unseen": what could not be seen, coming into sight. Drawn in one colour, it is a ring that is
thick at that point and thins away from it, with the point as a dot on it. The name is set in a
typeface of circles and straight lines, like the mark: "LibreELEC" small over "HADES".

## Where they go

In this folder, installed by the recipe of Kodi into `/usr/share/kodi/media`:

| File | What it is |
| --- | --- |
| `splash.jpg` | the screen while the interface starts: the mark over the name, on black |
| `vendor_logo.png` | the logo of the home screen and of the login screen: the mark and the name, white on nothing |
| `vendor_icon.png` | the small mark in the corner of every dialog, which the skin tints: the ring and its point of light |
| `icon16x16.png` to `icon256x256.png` | the icon of the program (event log, timers, what a network client shows for this device): the mark on a dark tile |
| `banner.png`, `applaunch_screen.png` | launch images of other platforms; not shown here, replaced so that the image holds no logo of Kodi as its own |
| `about/hades.png` | the picture of the page "About this build" of the system information, which takes the place of the page of Kodi's donations (`media/qr/kodilove`, removed) |

Each has the size of the file that it replaces.

`kodi-media.txt` is the list of the files that Kodi installs in that folder and its subfolders, but
for its fonts, as it was when these were made. The recipe stops the build if the files there are
not the ones of the list, by their names: a logo that a later Kodi adds, moves or renames is then
seen, not shipped. (It looks at names of files; it does not see a file whose content changed, nor
a link.) The one file of the list that stays as Kodi has it is the picture of a site for games,
which is not Kodi's.

Elsewhere in the tree:

| File | What it is |
| --- | --- |
| `distributions/LibreELEC/splash/splash-720.png` to `splash-2160.png` | the screen while the system starts, one for each of eleven heights of screen. LibreELEC's five carried its logo and the words "Just enough OS for KODI" |
| `packages/mediacenter/LibreELEC-settings/brand/logo.png`, `wizard.png` | the logo at the foot of the window of LibreELEC's settings and the side of its first-run wizard, which carried the same; installed by the recipe of that add-on |

`logo.png` has the size of the file that it replaces. `wizard.png` has not: the add-on draws it into
a place of 270 by 676 whatever its proportions, so it has those proportions, at twice that size
(LibreELEC's own, 318 by 732, is drawn narrower than it is).

A user's own `splash.jpg` or `splash.png` in `/storage/.kodi/media` takes the place of the splash
screen of the interface, and an `oemsplash.png` on the boot partition the place of the one of the
system, as in Kodi and LibreELEC.

After an update, the interface may show a picture that it has kept (the icon in the event log, for
example) for up to a day: it looks again at a picture that it knows only then.

## The start of the system, and the start of the interface after it

The interface draws `splash.jpg`, a picture of 1920 by 1080, as high as the screen and in its middle
(as wide as the screen, on a screen wider than sixteen to nine). The pictures of the start of the
system are that same picture at the size it has on a screen of their height, cut to the width of
the narrowest screen of that height (five to four); the one of 1080 lines is, pixel for pixel, the
middle of the picture that `splash.jpg` was made from. On a screen from five to four to sixteen to
nine, one runs into the other without a change of size or place.

For that, the program that draws the picture of the start must not do what it does in LibreELEC:
make the picture as wide and as high as the screen, whatever its proportions (a circle is then no
circle on any screen of other proportions than the picture). The patch
`packages/tools/plymouth-lite/patches/plymouth-lite-9000-*` gives it the word `fit`: the picture
keeps its proportions, as large as fits, in the middle, on black. The init says that word for the
pictures of the build. A picture of the user (`oemsplash.png`) is stretched as it always was.

The init takes the picture of the height of the screen, or the one of 1080 lines where there is
none of that height; the program then makes that one larger or smaller with the pixels it has (it
does not smooth). The heights are the ones of the screens there are: 720, 768, 800, 900, 1024, 1050,
1080, 1200, 1440, 1600 and 2160 lines.

## How they are made

`src/make-brand-images.py` computes them; nothing is traced from another picture. It needs Python 3
with Pillow (built with FreeType) and NumPy, and writes three folders (`kodi`, `boot`, `settings`):

    python3 src/make-brand-images.py --jost src/Jost-VariableFont_wght.ttf --out <folder>

- The light is computed in floating point and brought to 8 bits with noise. A soft glow on black
  shows bands on a good display otherwise. The noise leaves the mean of the pixels where the light
  is, and its strength is the same whatever the light: a slow change of light shows no bands of
  grain either. Only within half a step of black (or of the colour of the tile, in the pictures
  that have one) and of white is it weaker, down to none.
- Where there is no light the picture is exactly black, so that an OLED panel stays off there. (In
  the JPEG, a few pixels within some eight pixels of the light are not: that is the compression.)
- The splash screen of the interface is a JPEG without colour subsampling at quality 98.

The ones here were made with Pillow 12.1.1 (FreeType 2.14.2, libjpeg-turbo and zlib as Ubuntu 26.04
has them) and NumPy 2.3.5. The noise has a fixed seed, the letters are placed with Pillow's basic
layout whether or not it has another, and a value is rounded before the noise decides its pixel, so
that the last digits of the arithmetic do not: two runs gave the same files, also with the fast
paths of the processor that NumPy uses turned off. Other versions of those libraries may give
other files.

## The typeface

Jost, by indestructible type* (The Jost Project Authors,
https://github.com/indestructible-type), under the SIL Open Font License 1.1 (`src/OFL.txt`).
`src/Jost-VariableFont_wght.ttf` is the file `ofl/jost/Jost[wght].ttf` of
https://github.com/google/fonts at commit `6e4b84c976cadb3c49a40fd9a1c203e4f7fcf2da`, unchanged but
for its file name:

    6343b70971000b04c5d401c96ae08ce371086135e999d5e1e1413039c0213076  Jost-VariableFont_wght.ttf

It is here as the source of the pictures; the image of the build holds only the pictures.

The pictures and the script are this project's own work, under the licence of the recipe
(GPL-2.0-or-later).

## What does not change

The texts are changed by a patch of Kodi (`ProductName.h`). The names that other programs read stay
Kodi's (paths, the user agent, JSON-RPC, UPnP and AirPlay identifiers, the default name of the
device): they say what the program is compatible with. Add-ons that are Kodi's own keep their names
and icons, among them Kodi's add-on repository and its web interface; and the settings add-on of
LibreELEC keeps its own icon and name.
