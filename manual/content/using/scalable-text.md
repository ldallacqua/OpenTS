---
title: Scalable text
summary: "Draw dialog, match, and score text with scalable fonts while keeping the original layout."
category: configuration
source_files:
  - code/options.cpp
  - code/sharptext.cpp
  - code/scaledface.cpp
  - code/grphmimg.cpp
  - code/ui/uishell.cpp
---

Set these options in `SUN.INI` to draw dialog text and enlarged match and score text with scalable fonts:

```ini
[Options]
BitmapDialogFont=no
BitmapGameFont=no
```

Both options default to `yes`, which preserves the bitmap fonts. The UI's menu labels already use a scalable face when enlarged.

`BitmapDialogFont=no` keeps the dialog font's original layout metrics while drawing its letters with Segoe UI Semibold, or the shipped Arimo face when the Windows font is unavailable.

`BitmapGameFont=no` replaces enlarged battlefield and sidebar labels, software menu text, and campaign and multiplayer score text. The match labels keep their colors, alignment, and shadows, and fit their descenders inside the original line height. The score letters keep their color, their capital height, the position where each word starts, and the typing animation; within a word the letters take the scalable face's own widths and spacing, so a word can end a little left or right of where the bitmap word ended.

The options of the stock main menus and the game select screen are pictures with their lettering painted in. `BitmapGameFont=no` also draws that lettering in a scalable face, TeX Gyre Adventor, shipped as `texgyreadventor-bold.otf` in the `ui` directory. Each option keeps its letter color, its shadow, and the glow of its picture in every state. A picture whose lettering does not fit the English wording, such as another language's artwork, is shown unchanged, and so are all of them when the face file is missing.

The `GDI` and `NOD` names painted into the campaign score screen are drawn in Arial Bold from the Windows font folder, and stay bitmap lettering when that font is missing. Game logos remain artwork.

Match text follows `InterfaceScale` and `ViewScale`; score text follows `MenuScale`. A surface displayed at its original size keeps its bitmap text. Enlarged software text also keeps the bitmap font if no scalable face can be loaded. Text covered by later drawing is clipped around that drawing, or keeps its bitmap pixels when too little of the original text remains.

These options change presentation. They do not change game data, saved games, or multiplayer packets.
