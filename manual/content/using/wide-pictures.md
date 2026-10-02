---
title: Wide pictures
summary: "Replace a full-screen menu, score, or briefing picture with a high-resolution one that can be wider than the original."
category: configuration
source_files:
  - code/widepicture.cpp
  - code/sharptext.cpp
  - code/winstub.cpp
  - code/msanim.cpp
  - code/ui/rml/rmlrender.cpp
  - code/ui/screens/restate/uirestatedlg.cpp
---

Put a PNG file in the `HD` folder of the game data directory to replace a full-screen 640 by 400 picture with a high-resolution one. OpenTS ships no such pictures.

Name the file after the picture it replaces, followed by the CRC-32 checksum of that picture file's contents as eight lowercase hexadecimal digits:

```text
HD\score.7003ad74.png
```

This file replaces a `SCORE.PCX` whose contents have the checksum `7003ad74`. A `SCORE.PCX` with other contents, such as the other side's or a mod's, is left as it is until it has a file of its own.

The whole height of the PNG stands for the 400 rows of the original, and its middle stands for the original's 640 columns. A PNG wider than 8:5 therefore reaches beyond the original on both sides and takes the place of the mirrored bars there. A 16:9 screen is filled by a picture of 3840 by 2160 pixels or one of the same shape. The picture is resized to the height it is shown at, so one made for the screen's own height is shown pixel for pixel.

Two kinds of screen use the folder:

- The mission briefing screen shows the wide picture for `SCORE.PCX` whenever the file exists.
- The title, menu, and score screens show it when the menu frame is enlarged and `BitmapGameFont=no`; see [Scalable text](/using/scalable-text/). There the wide picture is drawn wherever the screen still shows the original picture. Anything the game drew over the original, such as a score panel, keeps its enlarged pixels, and a screen that shows less than a tenth of the original's pixels that are not black is not changed.

Movies keep the mirrored bars. Where a wide picture does not reach the edge of a wider screen, the bars remain beside it.

Wide pictures change presentation only. They do not change game data, saved games, or multiplayer packets.
