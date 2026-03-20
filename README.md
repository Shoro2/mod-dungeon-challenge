# mod-dungeon-challenge

Ein **Mythic+ inspiriertes Dungeon-Challenge-System** für AzerothCore (WoW 3.3.5a WotLK).

## Features

- **Schwierigkeitsstufen 1-20**: Skaliert Mob-HP und -Schaden progressiv
- **Timer-System**: Jeder Dungeon hat einen individuellen Timer mit Zeitdruck
- **Zufällige Affixe**: ~5% aller Mobs im Dungeon erhalten spezielle Fähigkeiten
- **10 verschiedene Affixe**: Bolstering, Raging, Sanguine, Necrotic, Bursting, Explosive, Fortified, Volcanic, Storming, Inspiring
- **Bestenliste**: Globale Rangliste pro Dungeon und Schwierigkeitsstufe
- **Gold-Belohnungen**: Skaliert mit Schwierigkeit, Bonus für In-Time-Completions
- **Tod-Strafe**: Jeder Tod kostet 5 Sekunden Timer
- **NPC-basierte UI**: Kein Addon benötigt — alles über Gossip-Menüs

## Unterstützte Dungeons

| Dungeon | Timer |
|---------|-------|
| Utgarde Keep | 25 Min |
| Utgarde Pinnacle | 28 Min |
| The Nexus | 28 Min |
| The Oculus | 35 Min |
| Culling of Stratholme | 30 Min |
| Halls of Stone | 28 Min |
| Drak'Tharon Keep | 25 Min |
| Azjol-Nerub | 20 Min |
| Halls of Lightning | 28 Min |
| Gundrak | 28 Min |
| Violet Hold | 25 Min |
| Ahn'kahet: The Old Kingdom | 30 Min |
| The Forge of Souls | 22 Min |
| Trial of the Champion | 25 Min |
| Pit of Saron | 28 Min |
| Halls of Reflection | 25 Min |

## Installation

### 1. Modul in AzerothCore einbinden

```bash
cd azerothcore-wotlk/modules/
git clone https://github.com/Shoro2/mod-dungeon-challenge.git
```

Oder als Symlink:
```bash
ln -s /pfad/zu/mod-dungeon-challenge azerothcore-wotlk/modules/mod-dungeon-challenge
```

### 2. Server neu kompilieren

```bash
cd azerothcore-wotlk/build
cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/azeroth-server \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DSCRIPTS=static -DMODULES=static
make -j$(nproc)
make install
```

### 3. Datenbank-Tabellen erstellen

Die SQL-Dateien werden beim ersten Start automatisch ausgeführt, wenn `include.sh` korrekt konfiguriert ist.

**Manuell:**

```bash
# World-Datenbank
mysql -u root acore_world < modules/mod-dungeon-challenge/data/sql/db-world/00_dungeon_challenge_world.sql

# Characters-Datenbank
mysql -u root acore_characters < modules/mod-dungeon-challenge/data/sql/db-characters/00_dungeon_challenge_characters.sql
```

### 4. Konfiguration anpassen

Kopiere die Konfigurationsdatei:
```bash
cp modules/mod-dungeon-challenge/conf/mod_dungeon_challenge.conf.dist etc/mod_dungeon_challenge.conf
```

Passe die Werte in `mod_dungeon_challenge.conf` nach Bedarf an (siehe Konfiguration unten).

### 5. Server starten

```bash
./worldserver
```

Beim Start sollte erscheinen:
```
>> mod-dungeon-challenge: Configuration loaded (Enabled: Yes, MaxDifficulty: 20, AffixPct: 5%)
>> mod-dungeon-challenge: Loaded 16 default dungeons.
>> mod-dungeon-challenge: Loaded 10 affixes.
```

## Ingame-Anleitung

### NPC spawnen

Der Challenge-NPC hat die Entry-ID **500000**. Spawne ihn in einer Hauptstadt:

```
.npc add 500000
```

### Challenge starten

1. **Gruppe bilden** (2-5 Spieler)
2. **NPC ansprechen** → Hauptmenü öffnet sich
3. **"Dungeon Challenge starten"** wählen
4. **Dungeon auswählen** aus der Liste
5. **Schwierigkeitsstufe wählen** (1-20)
   - Grün (1-5): Einstieg
   - Gelb (6-10): Mittel
   - Orange (11-15): Schwer
   - Rot (16-20): Extrem
6. **Bestätigen** → Gruppe wird in den Dungeon teleportiert
7. **Timer startet** → Alle Bosse vor Ablauf besiegen!

### Während des Runs

- **Timer**: Verbleibende Zeit wird periodisch im Chat angezeigt
- **Boss-Kills**: Fortschritt wird nach jedem Boss-Kill angezeigt
- **Tode**: Jeder Tod kostet 5 Sekunden und wird angekündigt
- **Affixe**: ~5% der Mobs haben spezielle Fähigkeiten (im Chat markiert)
- **Abbrechen**: Über den NPC oder Gruppenleiter kann der Run abgebrochen werden

### Bestenliste

Über den NPC:
- **"Bestenliste"** → Dungeon auswählen → Top 10 Runs
- **"Meine besten Runs"** → Persönliche Bestleistungen

## Konfiguration

| Einstellung | Standard | Beschreibung |
|-------------|----------|--------------|
| `DungeonChallenge.Enable` | 1 | Modul aktivieren/deaktivieren |
| `DungeonChallenge.AffixPercentage` | 5 | % der Mobs mit zufälligen Affixen |
| `DungeonChallenge.MaxDifficulty` | 20 | Höchste wählbare Stufe |
| `DungeonChallenge.HealthMultiplierPerLevel` | 15 | HP-Bonus pro Stufe in % |
| `DungeonChallenge.DamageMultiplierPerLevel` | 8 | Schaden-Bonus pro Stufe in % |
| `DungeonChallenge.TimerBaseMinutes` | 30 | Fallback-Timer wenn kein Dungeon-spezifischer Timer |
| `DungeonChallenge.LootBonusPerLevel` | 50000 | Gold pro Stufe in Copper (50000 = 5 Gold) |
| `DungeonChallenge.NpcEntry` | 500000 | Creature Entry-ID des NPCs |
| `DungeonChallenge.AnnounceOnLogin` | 1 | Begrüßungsnachricht beim Login |

## Schwierigkeitsskalierung

| Stufe | HP-Multiplikator | DMG-Multiplikator | Aktive Affixe |
|-------|-----------------|-------------------|---------------|
| 1 | 1.15x | 1.08x | 0 |
| 2 | 1.30x | 1.16x | 3 (Bolstering, Raging, Fortified) |
| 5 | 1.75x | 1.40x | 3 |
| 7 | 2.05x | 1.56x | 5 (+Bursting, Explosive) |
| 10 | 2.50x | 1.80x | 7 (+Sanguine, Necrotic) |
| 14 | 3.10x | 2.12x | 9 (+Volcanic, Storming) |
| 15 | 3.25x | 2.20x | 10 (+Inspiring) |
| 20 | 4.00x | 2.60x | 10 |

## Affix-Beschreibungen

| Affix | Effekt | Tipp |
|-------|--------|------|
| **Bolstering** | Mob-Tod buffet nahestehende Allies (+20% HP/DMG) | Mobs gleichzeitig töten |
| **Raging** | Enrage unter 30% HP (+50% DMG) | Schnell fokussieren |
| **Sanguine** | Hinterlässt Heilzone für andere Mobs | Mobs aus der Zone ziehen |
| **Necrotic** | Stackende Heilungsreduktion | Tank muss kiten |
| **Bursting** | AoE-Schaden bei Mob-Tod | Nicht zu viele gleichzeitig töten |
| **Explosive** | Spawnt explosive Kugeln | Kugeln sofort zerstören |
| **Fortified** | +40% HP, +20% DMG | Mehr Zeit für Trash einplanen |
| **Volcanic** | Feuerzonen unter Fernkämpfern | Ständig bewegen |
| **Storming** | Bewegliche Tornados | Ausweichen |
| **Inspiring** | Allies immun gegen CC | Inspiring-Mob zuerst töten |

## Dungeon-Timer anpassen

Die Timer können in der `dungeon_challenge_dungeons` Tabelle in der World-DB angepasst werden:

```sql
UPDATE `dungeon_challenge_dungeons` SET `timer_minutes` = 35 WHERE `map_id` = 574;
```

## Eigene Dungeons hinzufügen

```sql
INSERT INTO `dungeon_challenge_dungeons`
(`map_id`, `name`, `entrance_x`, `entrance_y`, `entrance_z`, `entrance_o`, `timer_minutes`, `boss_count`)
VALUES (999, 'Mein Custom Dungeon', 100.0, 200.0, 50.0, 0.0, 30, 4);
```

## Fehlerbehebung

### Modul wird nicht geladen
- Prüfe ob `modules/mod-dungeon-challenge/` existiert
- Prüfe die Build-Ausgabe auf Compile-Fehler
- Prüfe die Worldserver-Logs auf `mod-dungeon-challenge` Einträge

### NPC zeigt kein Menü
- Prüfe ob `creature_template` Entry 500000 existiert: `.lookup creature 500000`
- Prüfe ob ScriptName korrekt ist: `npc_dungeon_challenge`

### Teleport funktioniert nicht
- Prüfe die Eingangskoordinaten in `dungeon_challenge_dungeons`
- Koordinaten mit `0,0,0` müssen manuell nachgetragen werden
- Tipp: Gehe zum Dungeon-Eingang und nutze `.gps` für die Koordinaten

## Lizenz

Dieses Modul ist unter der GNU GPL v2 lizenziert, kompatibel mit AzerothCore.
