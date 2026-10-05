# mod-dungeon-challenge — open items

## Snapshot / lifecycle qualification

- **(high)** Coordinator-reviewed publication and later approved joint host rollout of snapshot4112/MIG081; local native348 and exact owned cleanup are accepted. Retain the disclosed empty unbound instance8 normal-unload residual.
- **(high)** Implement and qualify the separate fresh-re-entry contract: interrupted old run ends; a real Lua/AIO Start must enter a fresh instance, clear only selected map/difficulty bindings, preserve unrelated binds/old records/rewards, and handle the group leader without forcing remaining participants. Current same-map Start seam is source-confirmed but not yet runtime-reproduced; no checkpoint resume.

## Display

### Lua affix percentage is hardcoded and drifted

`lua_scripts/dungeon_challenge_server.lua:26` displays a fixed affix percentage of 10
while C++ is authoritative and the deployed config
(`dcore/configs/modules/mod_dungeon_challenge.conf`) is 40. Display-only, but it
misinforms players. Correctness item, not a performance one.

---

## Fixed

- **`RemoveChallengeRun` use-after-free** (found and fixed 2026-07-25).
  `CreateChallengeRun` cached `&_activeRuns[instanceId]` on the map's DataMap
  (`DungeonChallenge.cpp:413`) and `RemoveChallengeRun` erased the container node
  without clearing it, so `ProcessCreature` (`:550`) dereferenced freed memory after a
  run ended — the null check there could not help, because the pointer was non-null and
  stale. `RemoveChallengeRun` now clears `MapChallengeData::run` before erasing, via
  `sMapMgr->FindMap(mapId, instanceId)`, and does nothing if the instance map is already
  gone (the cached data died with it).
