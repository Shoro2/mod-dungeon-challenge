# CLAUDE.md — mod-dungeon-challenge

Technische Dokumentation für KI-Assistenten, die an diesem Modul arbeiten.

## Modul-Übersicht

**mod-dungeon-challenge** ist ein AzerothCore 3.3.5a Modul, das ein Mythic+-ähnliches Dungeon-Challenge-System implementiert. Spieler wählen per NPC-Gossip einen Dungeon und eine Schwierigkeitsstufe (1-20), werden teleportiert und müssen alle Bosse innerhalb eines Timers besiegen. ~5% der Dungeon-Mobs erhalten zufällige Affixe (spezielle Fähigkeiten/Modifikatoren).

## Architektur

### Verzeichnisstruktur

```
mod-dungeon-challenge/
├── CMakeLists.txt                              # AzerothCore Build-Integration
├── include.sh                                  # SQL-Pfad-Registrierung
├── conf/
│   └── mod_dungeon_challenge.conf.dist         # Konfigurations-Template
├── data/sql/
│   ├── db-world/
│   │   └── 00_dungeon_challenge_world.sql      # NPC, Dungeon-Tabelle, Keystone, Spell Overrides
│   └── db-characters/
│       └── 00_dungeon_challenge_characters.sql  # Leaderboard, History, Snapshots
├── src/
│   ├── DungeonChallenge.h                      # Header: Alle Datenstrukturen + Manager + DataMap
│   ├── DungeonChallenge.cpp                    # Singleton-Manager Implementierung
│   ├── DungeonChallengeNpc.cpp                 # NPC Gossip-Menüs (CreatureScript)
│   ├── DungeonChallengeScripts.cpp             # Hooks: WorldScript, PlayerScript, CreatureScript, UnitScript, ItemScript
│   └── mod_dungeon_challenge_loader.cpp        # Entry Point
├── CLAUDE.md                                   # Dieses Dokument
└── README.md                                   # User-Anleitung
```

### Kern-Klassen

| Klasse/Struct | Datei | Zweck |
|---------------|-------|-------|
| `DungeonChallengeMgr` | DungeonChallenge.h/cpp | Singleton-Manager: Config, Runs, Affixe, Leaderboard, Snapshots, Spell Overrides |
| `ChallengeRun` | DungeonChallenge.h | Zustand eines aktiven Runs (Timer, Bosse, Deaths, Penalty) |
| `MapChallengeData` | DungeonChallenge.h | DataMap::Base für Map-Instanzen (Run-Referenz, Non-Mythic Lock, Keystone-Status) |
| `CreatureChallengeData` | DungeonChallenge.h | DataMap::Base für Creatures (processed, originalHealth, damageMultiplier, affix) |
| `SpellOverrideEntry` | DungeonChallenge.h | Per-Spell Damage-Tuning via DB |
| `BossKillSnapshot` | DungeonChallenge.h | Detaillierter Boss-Kill-Record für Leaderboards |
| `DungeonInfo` | DungeonChallenge.h | Dungeon-Metadaten (Map, Eingang, Timer, Bossanzahl) |
| `AffixInfo` | DungeonChallenge.h | Affix-Definition (Name, Beschreibung, Min-Difficulty) |
| `LeaderboardEntry` | DungeonChallenge.h | Bestenlisten-Eintrag |
| `DungeonChallengePendingStore` | DungeonChallenge.h | Speichert pending Challenges zwischen NPC-Auswahl und Teleport |
| `npc_dungeon_challenge` | DungeonChallengeNpc.cpp | CreatureScript für den Gossip-NPC |
| `DungeonChallengeWorldScript` | DungeonChallengeScripts.cpp | Config laden, Startup |
| `DungeonChallengePlayerScript` | DungeonChallengeScripts.cpp | Login, MapChange, Death |
| `DungeonChallengeCreatureScript` | DungeonChallengeScripts.cpp | Creature Update (Raging Affix, DataMap Processing) |
| `DungeonChallengeCreatureDeathScript` | DungeonChallengeScripts.cpp | Creature Death (Boss-Kill, Affixe, Non-Mythic Lock, Snapshots) |
| `DungeonChallengeUnitScript` | DungeonChallengeScripts.cpp | Damage-Modifikation via UnitScript Hooks |
| `DungeonChallengeKeystoneScript` | DungeonChallengeScripts.cpp | ItemScript für Schlüsselstein-Aktivierung |
| `DungeonChallengeTimerScript` | DungeonChallengeScripts.cpp | Timer-Anzeige + Keystone-Countdown (AllMapScript) |

### Design Patterns

1. **DataMap Pattern**: Custom-Daten werden via `DataMap::Base` an `Map` und `Creature` Objekte angehängt. `MapChallengeData` speichert Run-Referenz und Lock-Status. `CreatureChallengeData` speichert Verarbeitungs-Status, Original-HP, Damage-Multiplier und Affix.

2. **UnitScript Damage Hooks**: Schaden wird nicht mehr durch direkte Änderung der Base-Weapon-Damage skaliert, sondern über `ModifyMeleeDamage()`, `ModifySpellDamageTaken()` und `ModifyPeriodicDamageAurasTick()` Hooks. Der `extraDamageMultiplier` wird in `CreatureChallengeData` gespeichert.

3. **Spell Override System**: Per-Spell Damage-Tuning via DB-Tabelle `dungeon_challenge_spell_override`. Map-spezifische oder globale Overrides (modPct für Direct, dotModPct für DoTs).

4. **Non-Mythic Lock**: Wenn eine Kreatur stirbt bevor ein Schlüsselstein aktiviert wurde, wird die Instanz als "non-challenge" gesperrt. Verhindert Exploits mit teilweise gecleared Dungeons.

5. **Keystone Item System**: Spieler kaufen einen Schlüsselstein beim NPC. Nach NPC-Auswahl und Teleport muss der Schlüsselstein im Dungeon benutzt werden um den 10-Sekunden-Countdown und dann den Timer zu starten.

6. **Snapshot-basierte Aufzeichnungen**: Jeder Boss-Kill erzeugt einen detaillierten Snapshot-Record pro Teilnehmer in der `dungeon_challenge_snapshot` Tabelle.

### Datenfluss

```
Spieler spricht NPC an
    ↓
ShowMainMenu → ShowDungeonMenu → ShowDifficultyMenu → ShowConfirmMenu
    ↓
StartChallengeRun()
    ├─ Validierung (Gruppe, Leader, Größe)
    ├─ sDungeonChallengePending->AddPending(leaderGuid, mapId, difficulty)
    └─ TeleportTo() für alle Gruppenmitglieder
    ↓
OnPlayerMapChanged() (PlayerScript Hook)
    ├─ Prüft Non-Mythic Lock (IsInstanceLocked)
    ├─ Prüft sDungeonChallengePending
    ├─ CreateChallengeRun() → ChallengeRun Objekt + MapChallengeData Link
    └─ (Keystone enabled) State = PREPARING, wartet auf Schlüsselstein
    ↓
Schlüsselstein benutzt (ItemScript)
    ├─ Validierung (Leader, Gruppe, kein Kampf, nicht gesperrt)
    ├─ State = COUNTDOWN (10 Sekunden)
    ├─ AssignAffixesToCreatures() → ~5% Mobs bekommen Affixe + DataMap
    └─ OnMapUpdate() zählt Countdown herunter → StartRun()
    ↓
Dungeon läuft:
    ├─ OnAllCreatureUpdate() → ProcessCreature() via DataMap + Raging Check
    ├─ ModifyMeleeDamage/ModifySpellDamageTaken → Damage via extraDamageMultiplier
    ├─ OnAllCreatureJustDied() → Boss-Kill + Snapshot + Affix on-death + Non-Mythic Lock
    ├─ OnPlayerJustDied() → Death Counter + Penalty (konfigurierbar)
    └─ OnMapUpdate() → Timer-Anzeige mit Penalty
    ↓
AllBossesKilled()
    ├─ CompleteRun()
    ├─ SaveBossKillSnapshot() (Endboss-Record)
    ├─ SaveRunToLeaderboard() + SaveHistory()
    ├─ DistributeRewards() (Gold + neuer Schlüsselstein)
    └─ OnDestroyInstance() → Cleanup
```

## Konfiguration

| Config Key | Default | Beschreibung |
|------------|---------|-------------|
| `DungeonChallenge.Enable` | 1 | Modul an/aus |
| `DungeonChallenge.AffixPercentage` | 5 | % der Mobs mit Affixen |
| `DungeonChallenge.MaxDifficulty` | 20 | Maximale Schwierigkeitsstufe |
| `DungeonChallenge.HealthMultiplierPerLevel` | 15 | +HP% pro Stufe |
| `DungeonChallenge.DamageMultiplierPerLevel` | 8 | +DMG% pro Stufe (via UnitScript Hooks) |
| `DungeonChallenge.TimerBaseMinutes` | 30 | Basis-Timer (Fallback) |
| `DungeonChallenge.LootBonusPerLevel` | 50000 | Gold-Bonus pro Stufe (Copper) |
| `DungeonChallenge.NpcEntry` | 500000 | Creature Entry des NPCs |
| `DungeonChallenge.AnnounceOnLogin` | 1 | Login-Nachricht |
| `DungeonChallenge.DeathPenaltySeconds` | 15 | Zeitstrafe pro Tod (Sekunden) |
| `DungeonChallenge.KeystoneEnabled` | 1 | Schlüsselstein-System an/aus |
| `DungeonChallenge.KeystoneBuyCooldownMinutes` | 1440 | Kauf-Cooldown (Minuten) |

## Skalierungsformeln

```
HP-Multiplikator = 1.0 + (HealthMultiplierPerLevel/100 × Stufe)
  Stufe 1:  1.15x  |  Stufe 5:  1.75x  |  Stufe 10: 2.50x  |  Stufe 20: 4.00x

DMG-Multiplikator = 1.0 + (DamageMultiplierPerLevel/100 × Stufe)
  Via UnitScript Hooks (ModifyMeleeDamage, ModifySpellDamageTaken, ModifyPeriodicDamageAurasTick)
  Gespeichert in CreatureChallengeData::extraDamageMultiplier
  Stufe 1:  1.08x  |  Stufe 5:  1.40x  |  Stufe 10: 1.80x  |  Stufe 20: 2.60x

Spell Override: dungeon_challenge_spell_override Tabelle
  modPct = Direct Damage Modifier (-1 = kein Override)
  dotModPct = DoT Damage Modifier (-1 = kein Override)
  Map-spezifisch oder global (map_id = 0)

Gold-Belohnung = LootBonusPerLevel × Stufe × (inTime ? 2 : 1)
  Stufe 10 in Time: 100g  |  Stufe 10 über Time: 50g

Timer-Strafe pro Tod: +DeathPenaltySeconds (Default: 15s)
  Effektive Zeit = elapsedTime + penaltyTime
```

## Affix-System

### Verfügbare Affixe

| ID | Name | Effekt | Ab Stufe |
|----|------|--------|----------|
| 1 | Bolstering | Tod: Nahestehende Allies +20% HP/DMG (via DataMap) | 2 |
| 2 | Raging | Unter 30% HP: +50% DMG (via extraDamageMultiplier) | 2 |
| 3 | Sanguine | Tod: Heilt nahestehende Mobs um 20% | 4 |
| 4 | Necrotic | Nahkampf: Heilungsreduktion (Stack) | 4 |
| 5 | Bursting | Tod: 5% MaxHP AoE an alle Spieler | 7 |
| 6 | Explosive | Spawnt explosive Kugeln (periodisch) | 7 |
| 7 | Fortified | +40% HP, +20% DMG (via extraDamageMultiplier) | 2 |
| 8 | Volcanic | Feuerzonen unter entfernten Spielern | 10 |
| 9 | Storming | Bewegliche Tornados | 10 |
| 10 | Inspiring | Allies immun gegen CC/Interrupt | 14 |

### Affix-Zuweisung

1. Sammle alle lebenden, nicht-Boss, nicht-freundlichen Kreaturen im Dungeon
2. Berechne Anzahl: `max(1, totalCreatures × affixPercentage / 100)`
3. Shuffle-and-Pick: Zufällige Auswahl + zufälliger Affix aus dem verfügbaren Pool
4. Affix wird nur aus Affixen gewählt, deren `minDifficulty ≤ aktuelle Stufe`
5. Affix wird in `CreatureChallengeData::affix` gespeichert (DataMap)

### Implementierte Affix-Effekte

- **Fortified**: Bei Zuweisung: +40% HP direkt, +20% via extraDamageMultiplier (UnitScript)
- **Raging**: Per-Tick Check in `OnAllCreatureUpdate()` → hasEnraged Flag + extraDamageMultiplier *= 1.5
- **Bolstering**: In `OnAllCreatureJustDied()` → +20% HP + extraDamageMultiplier *= 1.2 via DataMap
- **Bursting**: In `OnAllCreatureJustDied()` → `EnvironmentalDamage()` an alle Spieler
- **Sanguine**: In `OnAllCreatureJustDied()` → `ModifyHealth()` auf nahestehende Mobs

### Noch nicht implementierte Affixe (TODO)

- **Necrotic**: Benötigt SpellScript für Healing-Reduction-Debuff
- **Explosive**: Benötigt TempSummon für explosive Orb-Creature
- **Volcanic**: Benötigt periodischen Spell + Position-Berechnung
- **Storming**: Benötigt Creature-Movement mit Tornado-Visual
- **Inspiring**: Benötigt Aura-Mechanik für CC/Interrupt-Immunität

## Datenbank-Tabellen

### World DB

| Tabelle | Zweck |
|---------|-------|
| `dungeon_challenge_dungeons` | Dungeon-Definitionen (MapID, Eingang, Timer, Bosse) |
| `dungeon_challenge_spell_override` | Per-Spell Damage-Tuning (spellId, mapId, modPct, dotModPct) |
| `creature_template` (Entry 500000) | Challenge-NPC |
| `item_template` (Entry 500001) | Schlüsselstein-Item |

### Characters DB

| Tabelle | Zweck |
|---------|-------|
| `dungeon_challenge_leaderboard` | Bestenliste (Top-Runs pro Dungeon/Stufe) |
| `dungeon_challenge_history` | Alle Runs eines Spielers |
| `dungeon_challenge_best` | Aggregierte Bestleistungen |
| `dungeon_challenge_snapshot` | Boss-Kill Aufzeichnungen (pro Boss, pro Teilnehmer) |

## IDs und Bereiche

| Typ | ID/Bereich | Verwendung |
|-----|-----------|------------|
| NPC Entry | 500000 | Dungeon Challenge NPC |
| Item Entry | 500001 | Dungeon Challenge Schlüsselstein |
| Gossip Actions | 1000-5999 | Menü-Navigation |
| Spell | 8599 | Enrage Visual (Raging Affix) |
| Maps | 574-668 | WotLK 5-Mann Dungeons |

## Code-Konventionen

- AzerothCore C++ Standard (siehe share-public/CLAUDE.md)
- 4-Space Einrückung, keine Tabs
- `UpperCamelCase` für Klassen und Methoden
- `_lowerCamelCase` für private Member
- `UPPER_SNAKE_CASE` für Enums/Konstanten
- Commit Messages: Conventional Commits (feat/fix/docs)
- Sprache: Deutsch für Benutzer-sichtbare Texte, Englisch für Code-Kommentare

## Bekannte Einschränkungen / TODOs

1. **Affix-Implementierung**: Necrotic, Explosive, Volcanic, Storming, Inspiring sind als Hooks vorbereitet aber nicht implementiert
2. **Boss-Erkennung**: Aktuell über `creature_template.rank >= 3` → manche Bosse haben rank < 3
3. **Timer-UI**: Aktuell nur Chat-Messages → könnte WorldState-basierte UI nutzen
4. **Prepared Statements**: Aktuell Format-String-basierte Queries → sollten Prepared Statements verwenden
5. **Creature Scaling**: `GetCreatureBySpawnIdStore()` muss auf korrekte API geprüft werden
6. **Instance Reset**: Kein automatischer Instance-Reset nach Run-Ende
7. **Snapshot Reload**: Snapshots werden beim Startup geladen, aber nicht periodisch aktualisiert
