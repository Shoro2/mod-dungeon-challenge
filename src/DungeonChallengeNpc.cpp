#include "DungeonChallenge.h"
#include "ScriptedGossip.h"
#include "Group.h"
#include "MapMgr.h"

// ============================================================================
// Gossip Menu IDs
// ============================================================================

enum DungeonChallengeGossip
{
    // Main Menu Actions
    GOSSIP_ACTION_MAIN_MENU         = 1000,
    GOSSIP_ACTION_SELECT_DUNGEON    = 1100,
    GOSSIP_ACTION_SELECT_DIFFICULTY = 1200,
    GOSSIP_ACTION_START_RUN         = 1300,
    GOSSIP_ACTION_LEADERBOARD       = 1400,
    GOSSIP_ACTION_MY_RUNS           = 1500,
    GOSSIP_ACTION_CURRENT_RUN_INFO  = 1600,
    GOSSIP_ACTION_ABANDON_RUN       = 1700,
    GOSSIP_ACTION_LEADERBOARD_DUNGEON = 1800,

    // Offsets
    GOSSIP_DUNGEON_OFFSET           = 2000,
    GOSSIP_DIFFICULTY_OFFSET        = 3000,
    GOSSIP_LEADERBOARD_OFFSET       = 4000,
};

// ============================================================================
// Player Selection State (temporary, per-session)
// ============================================================================

struct PlayerChallengeSelection
{
    uint32 selectedMapId;
    uint32 selectedDifficulty;
    std::string selectedDungeonName;
};

static std::unordered_map<ObjectGuid, PlayerChallengeSelection> _playerSelections;

// ============================================================================
// NPC Script: npc_dungeon_challenge
// ============================================================================

class npc_dungeon_challenge : public CreatureScript
{
public:
    npc_dungeon_challenge() : CreatureScript("npc_dungeon_challenge") { }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        if (!sDungeonChallengeMgr->IsEnabled())
        {
            player->PlayerTalkClass->SendCloseGossip();
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cffff0000[Dungeon Challenge]|r Das Modul ist derzeit deaktiviert.");
            return true;
        }

        ShowMainMenu(player, creature);
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
    {
        player->PlayerTalkClass->ClearMenus();

        // Dungeon Selection
        if (action >= GOSSIP_DUNGEON_OFFSET && action < GOSSIP_DIFFICULTY_OFFSET)
        {
            uint32 dungeonIndex = action - GOSSIP_DUNGEON_OFFSET;
            auto const& dungeons = sDungeonChallengeMgr->GetAllDungeons();
            if (dungeonIndex < dungeons.size())
            {
                auto& sel = _playerSelections[player->GetGUID()];
                sel.selectedMapId = dungeons[dungeonIndex].mapId;
                sel.selectedDungeonName = dungeons[dungeonIndex].name;
                ShowDifficultyMenu(player, creature);
            }
            return true;
        }

        // Difficulty Selection
        if (action >= GOSSIP_DIFFICULTY_OFFSET && action < GOSSIP_LEADERBOARD_OFFSET)
        {
            uint32 difficulty = action - GOSSIP_DIFFICULTY_OFFSET;
            auto& sel = _playerSelections[player->GetGUID()];
            sel.selectedDifficulty = difficulty;
            ShowConfirmMenu(player, creature);
            return true;
        }

        // Leaderboard Dungeon Selection
        if (action >= GOSSIP_LEADERBOARD_OFFSET)
        {
            uint32 dungeonIndex = action - GOSSIP_LEADERBOARD_OFFSET;
            auto const& dungeons = sDungeonChallengeMgr->GetAllDungeons();
            if (dungeonIndex < dungeons.size())
                ShowLeaderboardForDungeon(player, creature, dungeons[dungeonIndex].mapId,
                    dungeons[dungeonIndex].name);
            return true;
        }

        switch (action)
        {
            case GOSSIP_ACTION_MAIN_MENU:
                ShowMainMenu(player, creature);
                break;

            case GOSSIP_ACTION_SELECT_DUNGEON:
                ShowDungeonMenu(player, creature);
                break;

            case GOSSIP_ACTION_SELECT_DIFFICULTY:
                ShowDifficultyMenu(player, creature);
                break;

            case GOSSIP_ACTION_START_RUN:
                StartChallengeRun(player, creature);
                break;

            case GOSSIP_ACTION_LEADERBOARD:
                ShowLeaderboardMenu(player, creature);
                break;

            case GOSSIP_ACTION_MY_RUNS:
                ShowMyRuns(player, creature);
                break;

            case GOSSIP_ACTION_CURRENT_RUN_INFO:
                ShowCurrentRunInfo(player, creature);
                break;

            case GOSSIP_ACTION_ABANDON_RUN:
                AbandonCurrentRun(player, creature);
                break;

            default:
                ShowMainMenu(player, creature);
                break;
        }

        return true;
    }

private:
    // ========================================================================
    // Menu Builders
    // ========================================================================

    void ShowMainMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();

        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "|TInterface\\Icons\\Achievement_Dungeon_ClassicDungeonMaster:30:30|t Dungeon Challenge starten",
            GOSSIP_ACTION_MAIN_MENU, GOSSIP_ACTION_SELECT_DUNGEON);

        AddGossipItemFor(player, GOSSIP_ICON_TABARD, "|TInterface\\Icons\\INV_Misc_Trophy_Argent:30:30|t Bestenliste",
            GOSSIP_ACTION_MAIN_MENU, GOSSIP_ACTION_LEADERBOARD);

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|TInterface\\Icons\\Achievement_BG_KillXEnemies_GeneralsRoom:30:30|t Meine besten Runs",
            GOSSIP_ACTION_MAIN_MENU, GOSSIP_ACTION_MY_RUNS);

        // Show current run info if active
        if (player->GetMap() && player->GetMap()->IsDungeon())
        {
            ChallengeRun* run = sDungeonChallengeMgr->GetChallengeRun(player->GetInstanceId());
            if (run && (run->state == CHALLENGE_STATE_RUNNING || run->state == CHALLENGE_STATE_PREPARING))
            {
                AddGossipItemFor(player, GOSSIP_ICON_DOT, "|cff00ff00[AKTIV]|r Aktueller Run - Info",
                    GOSSIP_ACTION_MAIN_MENU, GOSSIP_ACTION_CURRENT_RUN_INFO);
                AddGossipItemFor(player, GOSSIP_ICON_DOT, "|cffff0000[ABBRECHEN]|r Aktuellen Run abbrechen",
                    GOSSIP_ACTION_MAIN_MENU, GOSSIP_ACTION_ABANDON_RUN);
            }
        }

        SendGossipMenuFor(player, creature->GetEntry(), creature->GetGUID());
    }

    void ShowDungeonMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();

        auto const& dungeons = sDungeonChallengeMgr->GetAllDungeons();
        for (uint32 i = 0; i < dungeons.size(); ++i)
        {
            std::string label = fmt::format("|TInterface\\Icons\\Spell_Shadow_SummonVoidwalker:20:20|t {} (Timer: {} Min)",
                dungeons[i].name, dungeons[i].timerMinutes);
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, label,
                GOSSIP_ACTION_SELECT_DUNGEON, GOSSIP_DUNGEON_OFFSET + i);
        }

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "<< Zurueck",
            GOSSIP_ACTION_SELECT_DUNGEON, GOSSIP_ACTION_MAIN_MENU);
        SendGossipMenuFor(player, creature->GetEntry(), creature->GetGUID());
    }

    void ShowDifficultyMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();

        auto it = _playerSelections.find(player->GetGUID());
        if (it == _playerSelections.end())
        {
            ShowMainMenu(player, creature);
            return;
        }

        std::string header = fmt::format("Dungeon: |cff00ff00{}|r\nWaehle die Schwierigkeitsstufe:",
            it->second.selectedDungeonName);

        uint32 maxDiff = sDungeonChallengeMgr->GetMaxDifficulty();

        // Show difficulties in groups of 5
        for (uint32 d = 1; d <= maxDiff; ++d)
        {
            float hpMult = sDungeonChallengeMgr->GetHealthMultiplier(d);
            float dmgMult = sDungeonChallengeMgr->GetDamageMultiplier(d);
            auto affixes = sDungeonChallengeMgr->GetAffixesForDifficulty(d);

            std::string color;
            if (d <= 5)       color = "|cff00ff00"; // green
            else if (d <= 10) color = "|cffffff00"; // yellow
            else if (d <= 15) color = "|cffff8000"; // orange
            else              color = "|cffff0000"; // red

            std::string label = fmt::format("{0}Stufe {1}|r  |cffaaaaaa(HP: x{2:.1f}, DMG: x{3:.1f}, Affixe: {4})|r",
                color, d, hpMult, dmgMult, affixes.size());

            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, label,
                GOSSIP_ACTION_SELECT_DIFFICULTY, GOSSIP_DIFFICULTY_OFFSET + d);
        }

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "<< Zurueck",
            GOSSIP_ACTION_SELECT_DIFFICULTY, GOSSIP_ACTION_SELECT_DUNGEON);
        SendGossipMenuFor(player, creature->GetEntry(), creature->GetGUID());
    }

    void ShowConfirmMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();

        auto it = _playerSelections.find(player->GetGUID());
        if (it == _playerSelections.end())
        {
            ShowMainMenu(player, creature);
            return;
        }

        auto& sel = it->second;
        float hpMult = sDungeonChallengeMgr->GetHealthMultiplier(sel.selectedDifficulty);
        float dmgMult = sDungeonChallengeMgr->GetDamageMultiplier(sel.selectedDifficulty);
        auto affixes = sDungeonChallengeMgr->GetAffixesForDifficulty(sel.selectedDifficulty);
        uint32 timer = sDungeonChallengeMgr->GetTimerForDungeon(sel.selectedMapId);

        // Build affix description
        std::string affixStr;
        for (auto affix : affixes)
        {
            AffixInfo const* info = sDungeonChallengeMgr->GetAffixInfo(affix);
            if (info)
            {
                if (!affixStr.empty())
                    affixStr += ", ";
                affixStr += info->name;
            }
        }
        if (affixStr.empty())
            affixStr = "Keine";

        std::string confirmText = fmt::format(
            "|cff00ff00Bestaetigen:|r\n\n"
            "Dungeon: |cffff8000{}|r\n"
            "Stufe: |cffff8000{}|r\n"
            "HP: |cffff0000x{:.1f}|r\n"
            "Schaden: |cffff0000x{:.1f}|r\n"
            "Timer: |cffffff00{} Minuten|r\n"
            "Affixe: |cffff8000{}|r\n\n"
            "~5% der Mobs erhalten zufaellige Affixe!",
            sel.selectedDungeonName, sel.selectedDifficulty, hpMult, dmgMult,
            timer / 60, affixStr);

        // Send as NPC text
        ChatHandler(player->GetSession()).PSendSysMessage("%s", confirmText.c_str());

        AddGossipItemFor(player, GOSSIP_ICON_BATTLE,
            "|TInterface\\Icons\\Achievement_Dungeon_ClassicDungeonMaster:30:30|t |cff00ff00GO! Challenge starten!|r",
            GOSSIP_ACTION_START_RUN, GOSSIP_ACTION_START_RUN);

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "<< Schwierigkeit aendern",
            GOSSIP_ACTION_START_RUN, GOSSIP_ACTION_SELECT_DIFFICULTY);

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "<< Dungeon aendern",
            GOSSIP_ACTION_START_RUN, GOSSIP_ACTION_SELECT_DUNGEON);

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "<< Hauptmenue",
            GOSSIP_ACTION_START_RUN, GOSSIP_ACTION_MAIN_MENU);

        SendGossipMenuFor(player, creature->GetEntry(), creature->GetGUID());
    }

    // ========================================================================
    // Run Start
    // ========================================================================

    void StartChallengeRun(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->SendCloseGossip();

        auto it = _playerSelections.find(player->GetGUID());
        if (it == _playerSelections.end())
        {
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cffff0000[Dungeon Challenge]|r Fehler: Keine Auswahl gefunden. Bitte erneut auswaehlen.");
            return;
        }

        auto& sel = it->second;

        // Validation: Must be in a group
        Group* group = player->GetGroup();
        if (!group)
        {
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cffff0000[Dungeon Challenge]|r Du musst in einer Gruppe sein!");
            return;
        }

        // Validation: Must be group leader
        if (group->GetLeaderGUID() != player->GetGUID())
        {
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cffff0000[Dungeon Challenge]|r Nur der Gruppenleiter kann eine Challenge starten!");
            return;
        }

        // Validation: Group must be 5 man
        if (group->GetMembersCount() > 5)
        {
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cffff0000[Dungeon Challenge]|r Maximal 5 Spieler erlaubt!");
            return;
        }

        // Get dungeon info
        DungeonInfo const* dungeonInfo = sDungeonChallengeMgr->GetDungeonInfo(sel.selectedMapId);
        if (!dungeonInfo)
        {
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cffff0000[Dungeon Challenge]|r Dungeon nicht gefunden!");
            return;
        }

        // Announce to group
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->Next())
        {
            if (Player* member = ref->GetSource())
            {
                ChatHandler(member->GetSession()).PSendSysMessage(
                    "|cff00ff00[Dungeon Challenge]|r |cffff8000{}|r hat eine Challenge gestartet: "
                    "|cff00ff00{}|r auf Stufe |cffff8000{}|r!",
                    player->GetName(), dungeonInfo->name, sel.selectedDifficulty);

                ChatHandler(member->GetSession()).PSendSysMessage(
                    "|cff00ff00[Dungeon Challenge]|r Teleport in 5 Sekunden...");
            }
        }

        // Teleport all group members to dungeon entrance
        // We use a delayed approach: store the run data, teleport players
        // The InstanceScript hook will then set up the challenge

        // Store challenge data for the instance (keyed by leader GUID temporarily)
        // Will be properly linked once instance is created
        auto& pendingSel = _playerSelections[player->GetGUID()];

        // Teleport group to dungeon
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->Next())
        {
            if (Player* member = ref->GetSource())
            {
                member->TeleportTo(sel.selectedMapId,
                    dungeonInfo->entranceX, dungeonInfo->entranceY,
                    dungeonInfo->entranceZ, dungeonInfo->entranceO);
            }
        }

        // Store pending challenge info (will be picked up by OnPlayerMapChanged)
        sDungeonChallengePending->AddPending(player->GetGUID(),
            sel.selectedMapId, sel.selectedDifficulty);

        // Clean up selection
        _playerSelections.erase(player->GetGUID());
    }

    // ========================================================================
    // Leaderboard
    // ========================================================================

    void ShowLeaderboardMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();

        auto const& dungeons = sDungeonChallengeMgr->GetAllDungeons();
        for (uint32 i = 0; i < dungeons.size(); ++i)
        {
            AddGossipItemFor(player, GOSSIP_ICON_TABARD, dungeons[i].name,
                GOSSIP_ACTION_LEADERBOARD, GOSSIP_LEADERBOARD_OFFSET + i);
        }

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "<< Zurueck",
            GOSSIP_ACTION_LEADERBOARD, GOSSIP_ACTION_MAIN_MENU);
        SendGossipMenuFor(player, creature->GetEntry(), creature->GetGUID());
    }

    void ShowLeaderboardForDungeon(Player* player, Creature* creature, uint32 mapId, std::string const& dungeonName)
    {
        player->PlayerTalkClass->ClearMenus();

        auto entries = sDungeonChallengeMgr->GetLeaderboard(mapId, 0, 10);

        ChatHandler(player->GetSession()).PSendSysMessage(
            "|cff00ff00[Dungeon Challenge]|r Bestenliste: |cffff8000{}|r", dungeonName);

        if (entries.empty())
        {
            ChatHandler(player->GetSession()).PSendSysMessage(
                "  Noch keine Eintraege vorhanden.");
        }
        else
        {
            for (auto const& entry : entries)
            {
                uint32 min = entry.completionTime / 60;
                uint32 sec = entry.completionTime % 60;
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "  |cffffcc00#{}|r Stufe |cffff8000{}|r - |cff00ff00{}:{:02}|r von |cff69ccf0{}|r ({} Tode)",
                    entry.rank, entry.difficulty, min, sec, entry.leaderName, entry.deathCount);
            }
        }

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "<< Zurueck zur Bestenliste",
            GOSSIP_ACTION_LEADERBOARD_DUNGEON, GOSSIP_ACTION_LEADERBOARD);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "<< Hauptmenue",
            GOSSIP_ACTION_LEADERBOARD_DUNGEON, GOSSIP_ACTION_MAIN_MENU);
        SendGossipMenuFor(player, creature->GetEntry(), creature->GetGUID());
    }

    void ShowMyRuns(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();

        auto entries = sDungeonChallengeMgr->GetPlayerBestRuns(player->GetGUID(), 10);

        ChatHandler(player->GetSession()).PSendSysMessage(
            "|cff00ff00[Dungeon Challenge]|r Deine besten Runs:");

        if (entries.empty())
        {
            ChatHandler(player->GetSession()).PSendSysMessage(
                "  Noch keine abgeschlossenen Challenges.");
        }
        else
        {
            for (auto const& entry : entries)
            {
                DungeonInfo const* info = sDungeonChallengeMgr->GetDungeonInfo(entry.mapId);
                std::string dungeonName = info ? info->name : "Unbekannt";
                uint32 min = entry.completionTime / 60;
                uint32 sec = entry.completionTime % 60;
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "  |cffff8000{}|r Stufe |cffff8000{}|r - |cff00ff00{}:{:02}|r ({} Tode)",
                    dungeonName, entry.difficulty, min, sec, entry.deathCount);
            }
        }

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "<< Zurueck",
            GOSSIP_ACTION_MY_RUNS, GOSSIP_ACTION_MAIN_MENU);
        SendGossipMenuFor(player, creature->GetEntry(), creature->GetGUID());
    }

    void ShowCurrentRunInfo(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();

        ChallengeRun* run = sDungeonChallengeMgr->GetChallengeRun(player->GetInstanceId());
        if (!run)
        {
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cffff0000[Dungeon Challenge]|r Kein aktiver Run gefunden.");
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "<< Zurueck",
                GOSSIP_ACTION_CURRENT_RUN_INFO, GOSSIP_ACTION_MAIN_MENU);
            SendGossipMenuFor(player, creature->GetEntry(), creature->GetGUID());
            return;
        }

        DungeonInfo const* info = sDungeonChallengeMgr->GetDungeonInfo(run->mapId);
        std::string dungeonName = info ? info->name : "Unbekannt";

        uint32 elapsed = run->elapsedTime;
        uint32 remaining = run->timerDuration > elapsed ? run->timerDuration - elapsed : 0;

        ChatHandler(player->GetSession()).PSendSysMessage(
            "|cff00ff00[Dungeon Challenge]|r Aktueller Run:\n"
            "  Dungeon: |cffff8000{}|r\n"
            "  Stufe: |cffff8000{}|r\n"
            "  Bosse: |cff00ff00{}/{}|r\n"
            "  Tode: |cffff0000{}|r\n"
            "  Verbleibend: |cffffff00{}:{:02}|r",
            dungeonName, run->difficulty, run->bossesKilled, run->totalBosses,
            run->deathCount, remaining / 60, remaining % 60);

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "<< Zurueck",
            GOSSIP_ACTION_CURRENT_RUN_INFO, GOSSIP_ACTION_MAIN_MENU);
        SendGossipMenuFor(player, creature->GetEntry(), creature->GetGUID());
    }

    void AbandonCurrentRun(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->SendCloseGossip();

        ChallengeRun* run = sDungeonChallengeMgr->GetChallengeRun(player->GetInstanceId());
        if (!run)
        {
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cffff0000[Dungeon Challenge]|r Kein aktiver Run gefunden.");
            return;
        }

        // Only leader can abandon
        if (run->leaderGuid != player->GetGUID())
        {
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cffff0000[Dungeon Challenge]|r Nur der Gruppenleiter kann den Run abbrechen!");
            return;
        }

        run->state = CHALLENGE_STATE_ABANDONED;

        // Notify all participants
        for (auto const& guid : run->participants)
        {
            if (Player* member = ObjectAccessor::FindPlayer(guid))
            {
                ChatHandler(member->GetSession()).PSendSysMessage(
                    "|cffff0000[Dungeon Challenge]|r Der Run wurde vom Gruppenleiter abgebrochen.");
            }
        }

        sDungeonChallengeMgr->RemoveChallengeRun(player->GetInstanceId());
    }

};

// ============================================================================
// Registration
// ============================================================================

void AddSC_npc_dungeon_challenge()
{
    new npc_dungeon_challenge();
}
