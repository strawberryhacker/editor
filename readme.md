# Terminal text editor

This is a simple terminal text editor. Some inspiration is taken from Kilo and Emacs. Some features are listed below. 

*(keyboard input, shortcuts are bad because they are read through stdin, also, the editor uses some OSC escape sequences which are not supported by all terminals, and some sequences may be ST (suckless) terminal only, also, it is not stable yet :))*

## Overview

- Renders only dirty regions.
- Multiple windows are supported. Swapping and resizing windows are supported. Splits, when resized, will try to maintain the original aspect ratio. Resizing the master window will resize child windows accordingly.
- Supports editing the same file in different windows.
- Supports a saved state per file per window such that the working location, mark, etc. is saved when changing window, and restored when going back.
- Each window have a separate minibar with various functions which does not reset when changing windows. The minibar has the same features as the main editor (scroll, word deletion, cursors, etc.)
- Error reporting in minibar.
- Separate minibar for new file and open file.
- Command minibar for miscellaneous stuff, such as window split.
- Small command parser.
- It is really simple to add commands, minibar modes, for extra functions.
- Support dynamically changable color themes (from command line).
- Support syntax highlighting based on extension (also changable from the theme) (based on Kilos implementation).
- Support smart deletion of tabs.
- Support smart indentation (brackets, tabsize following, etc.).
- Support word deletion (ctrl-del) the same way as in VScode.
- Support mark/block based editing (per window). A mark is silently set in the code upon a shortcut. The cursor can then be moved to a different place. Together they form a block.
- Support block based cut/copy/paste from a shared clipboard.
- Support regular scrolling with margins in place as well as page scrolling. 
- Support search based on the Boyer–Moore algorithm. The entire file is searched for a pattern, and all matches are highlighted. Match count etc. is visible on the minibar. The match closest to the cursor is marked. Matches can be browsed with the arrows. Esc resets to the previous location, enter moves cursor to the current match and closes the search minibar.
- Support saving file (I added a mark for unsaved files).
- Otherwise it is pretty customizable.

## Todo

- Undo/redo buffer.
- Tabs support (for makefiles, I don't know about this).
- Replace in file (auto replace with y/n confirmation).
- Reindent block.
- Reformat comments to have a linesize.
- Exiting while unsaved changes exist triggers save confirmation.
