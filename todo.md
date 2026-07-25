# mod-dungeon-challenge — open items

## Correctness

### `RemoveChallengeRun` leaves `MapChallengeData::run` dangling (use-after-free)

`CreateChallengeRun` stores a pointer into the `_activeRuns` container on the map's
DataMap:

- `DungeonChallenge.cpp:406` — `_activeRuns[instanceId] = run;`
- `DungeonChallenge.cpp:413` — `mapData->run = &_activeRuns[instanceId];`

`RemoveChallengeRun` then erases the container entry without clearing that pointer:

- `DungeonChallenge.cpp:422-425` — `_activeRuns.erase(instanceId);`

After a run ends, `mapData->run` therefore points at freed `unordered_map` node
storage, and `ProcessCreature` dereferences exactly that pointer
(`DungeonChallenge.cpp:550` — `if (!mapData->run || mapData->run->state != ...)`).
Reading it is undefined behaviour; the null check cannot help because the pointer is
non-null and stale.

**Fix**: clear `mapData->run` in `RemoveChallengeRun` (and anywhere else a run is
torn down), or stop caching a raw pointer and look the run up by instance id.

**Status**: not fixed. Found 2026-07-25 while reordering the per-tick hook for
performance. Deliberately *not* bundled into that performance commit — mixing a
correctness fix into a behaviour-neutral optimisation is exactly what the FL working
ritual forbids, because it makes both impossible to verify separately.

**Mitigation already in place**: the performance reorder in
`DungeonChallengeScripts.cpp::OnAllCreatureUpdate` now gates on
`GetChallengeRun(instanceId)` (which consults the authoritative `_activeRuns`) before
calling `ProcessCreature`, so the only path that read the dangling pointer is no
longer reachable while no run is registered. The dangling pointer itself remains.

## Display

### Lua affix percentage is hardcoded and drifted

`lua_scripts/dungeon_challenge_server.lua:26` displays a fixed affix percentage of 10
while C++ is authoritative and the deployed config
(`dcore/configs/modules/mod_dungeon_challenge.conf`) is 40. Display-only, but it
misinforms players. Correctness item, not a performance one.
