Digimon World 2003, the RK3326 port. Nothing of the game is in this folder.

Yours: game/ (your disc: the .cue and the .bin), settings.json, pad.map, tui.state, card1.mcd, card2.mcd, logs/.
The port's: dw2003 (the game), dw3-menu.py, ledd.py, probe.sh, licenses/.

Logs: logs/game.log is the game's run (empty with DEBUG > LOGS off), menu.log is the menu's, log.txt is the launcher's.
DW3_FORCE_VIDEO (fbdev or sdl2) and DW3_FORCE_RENDERER (gles or software) override the launcher's choice.
Select + Start quits.