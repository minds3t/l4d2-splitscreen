Splitscreen Support for Left 4 Dead 2 (PC) - version @VERSION@
======================================================

Two players on one PC, on one screen, the way the Xbox 360 version does it:
a shared offline lobby where each player picks their own survivor, a split
screen with a HUD for each player, and button prompts that match the device
each player holds.


What you need
-------------
- Windows and the Steam version of Left 4 Dead 2.
- One controller plus keyboard and mouse. Two controllers should work as
  well but have not been tested yet.


Start
-----
1. Unpack this folder anywhere and keep it together.
2. Double-click "Start Local Co-op.cmd".
   Or run "Install Shortcuts.cmd" once: it checks your game and puts a
   shortcut on the Desktop and in the Start menu.
3. In the main menu choose the OFFLINE CO-OP tile (one step left of
   Campaign) for a campaign, or the "Offline Co-op" row of another mode:
   Realism, Survival, Versus, Scavenge, Realism Versus, Versus Survival.
4. In the lobby each player chooses a character with their own device, then
   start the game.

The controller is player one, keyboard and mouse are player two (this can
be swapped, see Settings). Without a controller, keyboard and mouse are
player one. A controller that is switched on later is picked up.

During a game, player two can leave and come back from the pause menu:
Options > Disable Splitscreen / Enable Splitscreen.

Main menu > Options has two more rows: SPLITSCREEN DIRECTION (horizontal or
vertical), and Disable / Enable Splitscreen, which turns local co-op off and
on without leaving the game.


Good to know
------------
- The game is started with -insecure, as every game with a plugin must be.
  In a session started this way you cannot join VAC-protected servers. This
  package is for playing offline on one PC. Start the game normally through
  Steam for everything else.
- Nothing stays in the game folder. For each session the launcher copies
  the plugin, the settings and two packages into left4dead2\addons, and
  takes them out again when the game exits. For that time it also allows
  addons in the team modes this PC hosts (cfg\addonconfig.cfg) and puts
  that file back afterwards. If the PC crashed in between, the next start
  or "Uninstall.cmd" cleans up.
- A normal start through Steam is the unchanged game.
- If you still have the older VPK-only "Splitscreen Support" installed or
  subscribed in the Workshop, remove it: both change the same menus.


Settings
--------
Edit config.ini in this folder with a text editor. Changes apply at the
next start.

  [Input] KeyboardPlayer   2 = controller is player one, keyboard and mouse
                               are player two (default)
                           1 = keyboard and mouse are player one, the
                               controller is player two
  [Input] XboxSticks       1 = the Xbox game's stick response and look
                               sensitivity during local co-op (default)
                           0 = keep your own controller settings
  [Display] ConsoleUI      1 = the Xbox look (default): console lobby and
                               option pages, button bar in the menus, item
                               cross and console layouts in the HUD
                           0 = the PC look: Valve's PC menus and HUD; hints
                               still show each player's own buttons
  [Display] SafeArea       1 = the HUD keeps the console's distance from the
                               screen edge (default, Xbox look only)
                           0 = the HUD uses the whole screen
  [Launcher] GameArguments  extra game options for this launcher, for
                            example -novid (empty by default)


Known limits
------------
- Offline play on one PC only. No online games, no LAN guests.
- Mutations have no offline entry.
- Two controllers are untested.
- A game update can stop this package. It then says so, changes nothing,
  and needs a new version.


Remove
------
Run "Uninstall.cmd" (it removes the shortcuts and anything left in the game
folder), then delete this folder.


If something goes wrong
-----------------------
After a session the plugin's log is splitscreen_support.log in this folder. Include it
when you report a problem.


Credits
-------
Splitscreen Support by Minds3t.
You may use this package to play; it must not be redistributed or changed
without permission (terms: files\licenses\SplitscreenSupport.txt).
Uses MinHook by Tsuda Kageyu (license: files\licenses\MinHook.txt).
Left 4 Dead 2 is a trademark of Valve Corporation. This is an unofficial
modification and is not affiliated with Valve.
