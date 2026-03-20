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
│   │   └── 00_dungeon_challenge_world.sql      # NPC, Dungeon-Tabelle
│   └── db-characters/
│       └── 00_dungeon_challenge_characters.sql  # Leaderboard, History
├── src/
│   ├── DungeonChallenge.h                      # Header: Alle Datenstrukturen + Manager
│   ├── DungeonChallenge.cpp                    # Singleton-Manager Implementierung
│   ├── DungeonChallengeNpc.cpp                 # NPC Gossip-Menüs (CreatureScript)
│   ├── DungeonChallengeScripts.cpp             # Hooks: WorldScript, PlayerScript, CreatureScript
│   └── mod_dungeon_challenge_loader.cpp        # Entry Point
├── CLAUDE.md                                   # Dieses Dokument
└── README.md                                   # User-Anleitung
```

### Kern-Klassen

| Klasse/Struct | Datei | Zweck |
|---------------|-------|-------|
| `DungeonChallengeMgr` | DungeonChallenge.h/cpp | Singleton-Manager: Config, Runs, Affixe, Leaderboard |
| `ChallengeRun` | DungeonChallenge.h | Zustand eines aktiven Runs (Timer, Bosse, Deaths) |
| `DungeonInfo` | DungeonChallenge.h | Dungeon-Metadaten (Map, Eingang, Timer, Bossanzahl) |
| `AffixInfo` | DungeonChallenge.h | Affix-Definition (Name, Beschreibung, Min-Difficulty) |
| `LeaderboardEntry` | DungeonChallenge.h | Bestenlisten-Eintrag |
| `DungeonChallengePendingStore` | DungeonChallenge.h | Speichert pending Challenges zwischen NPC-Auswahl und Teleport |
| `npc_dungeon_challenge` | DungeonChallengeNpc.cpp | CreatureScript für den Gossip-NPC |
| `DungeonChallengeWorldScript` | DungeonChallengeScripts.cpp | Config laden, Startup |
| `DungeonChallengePlayerScript` | DungeonChallengeScripts.cpp | Login, MapChange, Death |
| `DungeonChallengeCreatureScript` | DungeonChallengeScripts.cpp | Creature Update (Raging Affix) |
| `DungeonChallengeCreatureDeathScript` | DungeonChallengeScripts.cpp | Creature Death (Boss-Kill, Affixe) |
| `DungeonChallengeTimerScript` | DungeonChallengeScripts.cpp | Timer-Anzeige (AllMapScript) |

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
    ├─ Prüft sDungeonChallengePending
    ├─ CreateChallengeRun() → ChallengeRun Objekt
    ├─ AssignAffixesToCreatures() → ~5% Mobs bekommen Affixe
    ├─ ScaleCreatureForDifficulty() → HP/DMG Skalierung
    └─ StartRun() → Timer startet
    ↓
Dungeon läuft:
    ├─ OnAllCreatureUpdate() → Raging-Affix Check
    ├─ OnAllCreatureJustDied() → Boss-Kill Tracking + Affix on-death
    ├─ OnPlayerJustDied() → Death Counter + Timer-Strafe
    └─ OnMapUpdate() → Timer-Anzeige
    ↓
AllBossesKilled()
    ├─ CompleteRun()
    ├─ SaveRunToLeaderboard()
    └─ DistributeRewards()
```

## Konfiguration

| Config Key | Default | Beschreibung |
|------------|---------|-------------|
| `DungeonChallenge.Enable` | 1 | Modul an/aus |
| `DungeonChallenge.AffixPercentage` | 5 | % der Mobs mit Affixen |
| `DungeonChallenge.MaxDifficulty` | 20 | Maximale Schwierigkeitsstufe |
| `DungeonChallenge.HealthMultiplierPerLevel` | 15 | +HP% pro Stufe |
| `DungeonChallenge.DamageMultiplierPerLevel` | 8 | +DMG% pro Stufe |
| `DungeonChallenge.TimerBaseMinutes` | 30 | Basis-Timer (Fallback) |
| `DungeonChallenge.LootBonusPerLevel` | 50000 | Gold-Bonus pro Stufe (Copper) |
| `DungeonChallenge.NpcEntry` | 500000 | Creature Entry des NPCs |
| `DungeonChallenge.AnnounceOnLogin` | 1 | Login-Nachricht |

## Skalierungsformeln

```
HP-Multiplikator = 1.0 + (HealthMultiplierPerLevel/100 × Stufe)
  Stufe 1:  1.15x  |  Stufe 5:  1.75x  |  Stufe 10: 2.50x  |  Stufe 20: 4.00x

DMG-Multiplikator = 1.0 + (DamageMultiplierPerLevel/100 × Stufe)
  Stufe 1:  1.08x  |  Stufe 5:  1.40x  |  Stufe 10: 1.80x  |  Stufe 20: 2.60x

Gold-Belohnung = LootBonusPerLevel × Stufe × (inTime ? 2 : 1)
  Stufe 10 in Time: 100g  |  Stufe 10 über Time: 50g

Timer-Strafe pro Tod: -5 Sekunden
```

## Affix-System

### Verfügbare Affixe

| ID | Name | Effekt | Ab Stufe |
|----|------|--------|----------|
| 1 | Bolstering | Tod: Nahestehende Allies +20% HP/DMG | 2 |
| 2 | Raging | Unter 30% HP: +50% DMG (Enrage-Visual) | 2 |
| 3 | Sanguine | Tod: Heilt nahestehende Mobs um 20% | 4 |
| 4 | Necrotic | Nahkampf: Heilungsreduktion (Stack) | 4 |
| 5 | Bursting | Tod: 5% MaxHP AoE an alle Spieler | 7 |
| 6 | Explosive | Spawnt explosive Kugeln (periodisch) | 7 |
| 7 | Fortified | +40% HP, +20% DMG (permanent) | 2 |
| 8 | Volcanic | Feuerzonen unter entfernten Spielern | 10 |
| 9 | Storming | Bewegliche Tornados | 10 |
| 10 | Inspiring | Allies immun gegen CC/Interrupt | 14 |

### Affix-Zuweisung

1. Sammle alle lebenden, nicht-Boss, nicht-freundlichen Kreaturen im Dungeon
2. Berechne Anzahl: `max(1, totalCreatures × affixPercentage / 100)`
3. Shuffle-and-Pick: Zufällige Auswahl + zufälliger Affix aus dem verfügbaren Pool
4. Affix wird nur aus Affixen gewählt, deren `minDifficulty ≤ aktuelle Stufe`

### Implementierte Affix-Effekte

- **Fortified**: Sofort bei Zuweisung (Stat-Modifikation)
- **Raging**: Per-Tick Check in `OnAllCreatureUpdate()` → Enrage-Aura bei <30% HP
- **Bolstering**: In `OnAllCreatureJustDied()` → Buff nahestehender Allies
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
| `creature_template` (Entry 500000) | Challenge-NPC |

### Characters DB

| Tabelle | Zweck |
|---------|-------|
| `dungeon_challenge_leaderboard` | Bestenliste (Top-Runs pro Dungeon/Stufe) |
| `dungeon_challenge_history` | Alle Runs eines Spielers |
| `dungeon_challenge_best` | Aggregierte Bestleistungen |

## IDs und Bereiche

| Typ | ID/Bereich | Verwendung |
|-----|-----------|------------|
| NPC Entry | 500000 | Dungeon Challenge NPC |
| Gossip Actions | 1000-4999 | Menü-Navigation |
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

1. **Entrance Coordinates**: Viele Dungeons haben `0,0,0` als Eingangskoordinaten → müssen in der DB nachgetragen werden
2. **Affix-Implementierung**: Necrotic, Explosive, Volcanic, Storming, Inspiring sind als Hooks vorbereitet aber nicht implementiert
3. **Boss-Erkennung**: Aktuell über `creature_template.rank >= 3` → manche Bosse haben rank < 3
4. **Timer-UI**: Aktuell nur Chat-Messages → könnte WorldState-basierte UI nutzen
5. **Belohnungen**: Aktuell nur Gold → Items/Tokens als Belohnung hinzufügen
6. **Prepared Statements**: Aktuell Format-String-basierte Queries → sollten Prepared Statements verwenden
7. **Creature Scaling**: `GetCreatureBySpawnIdStore()` muss auf korrekte API geprüft werden
8. **Instance Reset**: Kein automatischer Instance-Reset nach Run-Ende
