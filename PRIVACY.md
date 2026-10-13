# Privacy policy

SwanStationPS5 is free, open-source software (GPL-3.0-or-later) with no ads, no accounts of its own, no analytics and no telemetry. The developer receives no data from your console. This policy lists everything the app stores and every connection it can make.

Last updated: 2026-10-13.

## What stays on your console

Everything lives under `/data/SwanStationPS5/` (and the app under `/data/homebrew/PPSA98510/`):

- Settings (global and per game), profiles, save games, save states, quick resume, screenshots, covers and downloaded art.
- Play statistics (time played, achievement progress per game) in `stats.txt`.
- `achievements-log.txt`: your recent RetroAchievements unlocks, shown in "Your profile".
- `retroachievements.ini`: your RetroAchievements user name and **login token**. Your password is never stored on the console (the PC tool swaps it for the token once).
- `logs/SwanStationPS5.log`: a diagnostic log.

None of this is sent anywhere by the app. You can delete it at any time with an FTP client or with Settings > System > Reset to factory settings (which also removes the per-game settings).

## Connections the app makes

| To | When | What is sent | Why |
| --- | --- | --- | --- |
| retroachievements.org and media.retroachievements.org | Only if you signed in | Your user name and token, the hash of the disc you load, your unlocks and play session, and the User-Agent `SwanStationPS5/<version> (PS5) rcheevos/<version>` | Achievements, leaderboards, points and your avatar |
| api.github.com | If "Check for updates" is on | A request for the latest releases of `darkxex/SwanStationPS5` | The update check; installing an update downloads the release from GitHub |
| raw.githubusercontent.com | When a cover, bezel, cheat file or the game database is needed | The file name for the game (title or disc ID) | Covers (`xlenore/psx-covers`, `libretro-thumbnails`), bezels (`thebezelproject`), cheats (`libretro-database`, `duckstation/chtdb`) and the game database (`stenzek/duckstation`) |

These servers receive the usual network metadata (your IP address, and the User-Agent above) and are governed by their own privacy policies. Nothing else is sent, and the app talks to no server run by the developer.

## Phone page (local network)

If you turn on the phone page (Settings), the console serves a small web page on your **local network** so you can type text or sign in to RetroAchievements from your phone. It is not reachable from the internet unless you forward a port yourself. Turn it off in Settings when you don't use it.

## Children

The app collects no personal data and has no social features.

## Contact

Open an issue at <https://github.com/darkxex/SwanStationPS5/issues>.
