# MFQ3 Missions

Data-driven mission definitions. The game scans these folders and lists each
mission in the corresponding menu — drop in a new `missionN.mission` file and it
appears automatically (no recompile).

```
missions/
  training/    -> Play > Training submenu
    mission1.mission   "Intercept"
    mission2.mission   "Dogfight"
    mission3.mission   "Ground Strike"
    mission4.mission   "Furball"
  campaign/    -> Play > Campaign submenu (same format)
```

## Deploy

The game loads from `play/mfdata/missions/...` (the `play/` tree is gitignored
proprietary data). These tracked files are the source; copy them over to run:

```
robocopy missions play\mfdata\missions /MIR
```

## File format

One `mission { }` block per file, parsed with COM_Parse-style `{ }` blocks and
`"quoted"` strings. See `training/mission1.mission` for the annotated reference.

Key fields:
- `number`, `name`, `briefing` — menu ordering + display text
- `map`, `gametype` — what to load
- `player { vehicle, radar }` — what you fly in, radar mode at start
- `enemy { ... }` (repeatable) — vehicle/team/spawn/distance/altitude/facing/behaviour, optional `count`, future `waypoints { }`
- `objective { type, text }` — win condition
- `onComplete`, `onFail` — end-of-mission flow

This supersedes the hard-coded `MF_SpawnTrainingMission` switch (g_bot_cmds.c);
the loader reads these files and configures the same spawner.
