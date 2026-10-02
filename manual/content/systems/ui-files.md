---
title: UI files
summary: Holds the RmlUi documents, style sheets, dialog kit and fonts that draw the game's screens, in a `ui` directory beside the executable.
category: interface-controls
keys: []
related:
  - type: using
    id: game-data
  - type: format
    id: mix
---

The `ui` directory sits beside the executable and holds everything the screens are built from. The build places it where `Language.dll` is written, and the release package carries it.

| File | Role |
| --- | --- |
| `<screen>.rml` | One document per screen, named after it, such as `sound.rml` |
| `<screen>.rcss` | That document's own style sheet |
| `dialog.rml` | The template every document opens inside |
| `kit.rcss` | The dialog kit, which styles the controls |
| `side-<name>.rcss` | Rules every screen takes while that side is the player's, such as `side-gdi.rcss` and `side-nod.rcss` |
| `glow.png` | The glow the template draws around a screen |
| `cursor.png` | The arrow pointer shown over the screens, with its tip at the top-left pixel. [`SystemCursor`](/keys/systemcursor/) replaces it with the Windows pointer |
| `Arimo.ttf` | The face a font family uses when its own face is missing, with its license in `OFL.txt` |
| `texgyreadventor-bold.otf` | The face the main menus' lettering is drawn in under [scalable text](/using/scalable-text/), with its license in `GUST-FONT-LICENSE.txt` |

## The dialog kit

`kit.rcss` styles the controls the way the game's dialogs drew them, and `dialog.rml` is the frame around a screen: the wallpaper, the side bars and the glow. A document names `dialog.rml` as its template, which brings in the kit, and also links its own style sheet. The shipped screen sheets only place their controls; how the controls look comes from the kit.

The pictures, and the dialog font that captions and buttons are drawn in, are the game's interface art, read from its mix files. Where a picture is missing, the control draws a plain fill in its place.

The kit gives every layout size in `dp`, RmlUi's scaled pixel, so a screen matches the dialog it replaced at the game's own resolution and grows with the game picture when that is drawn larger.

A screen opens the way the original dialogs did: it slides out from the middle behind a pair of side bars, with the dialog sound once as it starts. Put `reveal="none"` on a document's `<body>` for a screen that should appear at once instead.

## How a file is found

A document names every file it uses by bare file name, and the game adds the `ui` directory to its search folders at startup. Those names then resolve in [the order the game searches](/formats/opents-ini/#the-order-files-are-searched-for-in): a loose copy found earlier in that order wins, and a copy inside a mix archive is used only where no loose file of that name exists. To replace a shipped document, style sheet or picture, put a file of the same name in a folder the game reaches first.

If a document, style sheet or font fails to load, the game logs the file name, the screen does not open, and the game carries on as though the player had closed it without choosing.

Outside a match the game mounts the no-side archives. When a scenario is read or a saved game loads, it replaces them with the archives of the player's side, and it mounts the no-side archives again when the match ends and the menus return. [MIX archives](/formats/mix/#theater-side-and-speech-archives) names both sets. After each change the game reads the pictures and the dialog font again: a screen opened after it uses the copies the mounted archives supply, and a screen already on show keeps what it was built with. These archives are searched after every loose file and every archive mounted at startup, so their copy of a name is used only when none of those holds it. They cannot replace a shipped document or style sheet.

To give each side its own version of a picture or font, put a copy in each side's `SIDEC<nn>.MIX` or `SIDENC<nn>.MIX`, and a neutral copy for the menus in `SIDEC00.MIX` or `SIDENC00.MIX`. Keep the name out of the `ui` directory and the archives mounted at startup; a copy there is used everywhere instead.

## Side style sheets

While a side is the player's, every screen also takes the rules in `side-<name>.rcss`, where `<name>` is the side's name from `[Sides]` in lower case. The sheet is applied after the screen's own style sheets, so its rule wins over a shipped rule with an equally specific selector. A side with no such file keeps the shipped look. The game ships `side-gdi.rcss` and `side-nod.rcss`, which color the objectives screen's text green and red.

The player's side is set when a scenario is read or a saved game loads. It is cleared when the match ends and the menus return, so no side sheet applies in the menus.

The sheet is found like any other file here, so a side's own archive can carry it. A rule in it reaches every screen with a matching element. To limit a rule to one screen, begin its selector with that screen's body, such as `body[data-model="restate"] .line.ink { color: #fc1c1c; }`.

## Strings

Write `[[TXT_NAME]]` in a document for an engine string, using an identifier name that `code/language/language.h` defines, such as `[[TXT_CANCEL]]`. The game substitutes the string as it lays the text out. An unknown name is left as typed, so the mistake shows on screen, and the debug log names it.

## Which screens are documents

These screens are documents:

- the [classic main menu](/systems/developer-mode/#the-main-menu-code-recognizer), the menus under it, and the version screen
- the campaign and multiplayer choosers
- the options menu, with its sound, display, game control and keyboard screens
- the skirmish setup, the map chooser and the random map generator
- the network lobbies
- the load, save and delete lists
- the in-game options, the abort question and the objectives screen
- the out-of-sync screen and the [reconnect dialog](/systems/reconnect-dialog/)
- the message boxes, and the notices shown while a game saves or loads

The score screens, the graphical main menu, the campaign map and the sidebar are drawn by the game's older systems and are not documents. The crash report remains a Windows dialog.
