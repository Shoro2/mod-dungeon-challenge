# mod-dungeon-challenge — open items

## Snapshot / lifecycle qualification

- **(high)** Approved joint host rollout of published snapshot4112/doc100b, MIG081. Native348 and exact owned cleanup accepted; R3 passively confirmed instance8 normal unload, all137 old instances/68 controls equal.
- **(high)** Review/build/qualify private fresh-re-entry candidate from100b: acknowledged outside handoff, leader ACK before member entry, normalized selected storage key only, matching group join once, ordinary Leave preserves binds. Early verified existing pending intent provides authoritative pre-departure active-run guard. Permanent raid binds are refused; locked-raid restart remains unsupported without an explicit selected-lockout contract. No compiler/runtime/client/deploy acceptance; destructive BEFORE skipped. Preferred one-AFTER plan uses existing CRTEST2/1431/GUID2448 only after fresh empty-memory-bind proof and separately reviewed ordinary Heroic mode/own empty fixtures/teardown; no mode/reset action granted. Auth pinned incremental build, updater/realm/ban/SecretMgr no-transition gates and named server-owned transport need precise qualification. Separate group/cancellation and summary-Leave acceptance remain owed; no checkpoint resume.

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
