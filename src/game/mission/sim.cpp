// Mission simulation driver: set-up and frame schedule of mis_run
// (1000:01DE), the mission event tick (2dbd:3F4C), insertion/extraction
// (1000:0028..00B0), music mood and alerts (1000:07A1..0842), time of day
// (1000:1964..19EE) and the order keys of cmd_order_keys (19ac:0CC3).
#include "game/campaign.h"
#include "game/mission/campaign_link.h"
#include "game/mission/sim.h"

#include "engine/palette_fade.h"
#include "engine/sound.h"
#include "game/mission/ai.h"
#include "game/mission/build.h"
#include "game/mission/combat.h"
#include "game/mission/craft.h"
#include "game/mission/entity.h"
#include "game/mission/exedata.h"
#include "game/mission/geo.h"
#include "game/mission/modern.h"
#include "game/mission/msg.h"
#include "game/mission/people.h"
#include "game/mission/sfx.h"
#include "game/mission/state.h"
#include "game/mission/teams.h"
#include "game/mission/world.h"
#include "game/mission/wquery.h"

namespace st::game::mission {

namespace {

SimHooks g_hooks;
std::function<void(bool)> g_vegHook;
std::function<void()> g_vegResetHook;

constexpr u16 kPostureNames = 0x1DA4;  // char* [4]

// "if g_time - last >= period then last = g_time and run", false at g_time == 0.
bool elapsedPeriod(Ticks& last, s32 period) {
    MissionState& S = ms();
    if (wrapSub(S.time, last) >= period) {
        last = S.time;
        return S.time != 0;
    }
    return false;
}

// view_set_mode (1000:23FB), view_set_first_person (2321), view_set_chase
// (2349): veg_update(1) runs BEFORE g_view_mode is written (the scenery code
// sees the old mode); mode 0xC (view_enter_map) does not refresh scenery.
void setViewMode(int mode) {
    if (g_hooks.viewModeChanging) g_hooks.viewModeChanging(mode);
    if (mode != 0x0C && g_vegHook) g_vegHook(true);
    ms().viewMode = u8(mode);
    if (g_hooks.viewModeChanged) g_hooks.viewModeChanged(mode);
}

// hud_show_weapon_lines (1000:109F), at the end of view_set_first_person / view_set_chase.
void hudShowWeaponLines() {
    MissionState& S = ms();
    S.toolInfoTime = S.grenadeInfoTime = S.weaponInfoTime = wrapAdd(S.time, 0x200);
}

void playMusic(int track) { engine::sound().play(track); }

void stopMusic() { engine::sound().stop(); }

} // namespace

void setSimHooks(const SimHooks& hooks) {
    g_hooks = hooks;
    setTimeCompressionHooks([] {
        if (g_hooks.clockResync) g_hooks.clockResync();
        if (g_hooks.fullRedraw) g_hooks.fullRedraw();
    });
}

void setVegetationHook(std::function<void(bool)> fn) { g_vegHook = std::move(fn); }
void setVegetationResetHook(std::function<void()> fn) { g_vegResetHook = std::move(fn); }

void simSetViewMode(int mode) { setViewMode(mode); }

Unit* unitFromBody(const Obj3D* body) { return unitOfBody(body); }

bool objectiveComplete(int i) {
    const MissionState& S = ms();
    const MciObjective& o = S.mci.objective[i];
    const int k = int(u16(o.kind));
    if (k == 1 || k == 3 || k == 4 || k == 7) {
        auto& v = worldObjects();
        return o.target_structure >= 0 && o.target_structure < int(v.size()) && (v[size_t(o.target_structure)]->flags & 1);
    }
    if (k == 2 || k == 5 || k == 6) {
        const Team* t = team(S.firstMtmGroup + o.target_team);
        return t && t->members[0] && (t->members[0]->mover->flags2 & mover_flag2::kObjectiveDone);
    }
    return false;
}

// ---------------------------------------------------------------------------
// Time of day (1000:1964..19EE)
// ---------------------------------------------------------------------------

void todSet(int hour, int minute) {
    MissionState& S = ms();
    S.todMinute = s8(minute);
    S.todHour = s8(hour);
    S.todSecond = s8((S.time >> 8) % 60);
    S.tMinute = S.time;
}

void todAdd(int hours, int minutes) {
    MissionState& S = ms();
    S.todMinute = s8(S.todMinute + minutes);
    if (S.todMinute > 59) {
        S.todHour = s8(S.todHour + 1);
        S.todMinute = s8(S.todMinute - 60);
    }
    if (S.todMinute < 0) {
        S.todHour = s8(S.todHour - 1);
        S.todMinute = s8(S.todMinute + 60);
    }
    S.todHour = s8(S.todHour + hours);
    if (S.todHour > 23) S.todHour = s8(S.todHour - 24);
    if (S.todHour < 0) S.todHour = s8(S.todHour + 24);
}

void todUpdate() {
    MissionState& S = ms();
    S.elapsed = s16(wrapSub(S.time, S.tMinute));
    if (elapsedPeriod(S.tMinute, 0x3C00)) {
        S.todMinute = s8(S.todMinute + 1);
        if (S.todMinute % 60 == 0) {
            S.todHour = s8(S.todHour + 1);
            S.todMinute = 0;
            if (S.todHour % 24 == 0) S.todHour = 0;
        }
    }
    S.todSecond = s8((S.time >> 8) % 60);
}

// ---------------------------------------------------------------------------
// Insertion / extraction (1000:0028..00B0)
// ---------------------------------------------------------------------------

void misStartInsertion() {
    MissionState& S = ms();
    setViewMode(7);
    msgQueueClear();
    msgShowDs(0x0230, 0x800);  // "Insertion"
    S.insertionClearTime = wrapAdd(S.time, 0xA00);
    if (Unit* pm = pointMan()) pm->mover->winded = 4;
}

void misInsertionClear() {
    setViewMode(0);
    hudShowWeaponLines();
    setViewMode(2);
    hudShowWeaponLines();
    if (g_vegHook) g_vegHook(false);
    msgQueueClear();
    msgShowDs(0x023B, 0x200);  // "Insertion craft clear."
}

void misStartExtraction() {
    MissionState& S = ms();
    if (S.extractionEnd != 0) return;
    if (wldIsCamp()) return;
    setViewMode(8);
    msgQueueClear();
    msgShowDs(0x0252, 0x1D00);  // "Extraction"
    S.extractionEnd = wrapAdd(S.time, 0x1D00);
    // 19ac:00B6 awd_evaluate_mission: casualty tally, then the award evaluator (front end).
    msnTallyCasualties();
    if (g_hooks.evaluateMission) g_hooks.evaluateMission();
    stopMusic();
    int track = 6;
    Unit* pm = pointMan();
    if (pm && (pm->status->hit_mask & hit_bit::kKilled)) track = 9;
    else if (g_hooks.missionWon && g_hooks.missionWon(S.stats.missionScore)) track = 7;
    else if (S.time < S.contactUntil) track = 10;
    else if (statCountDead(0) > 1) track = 8;
    playMusic(track);
    S.extracting = true;
}

// ---------------------------------------------------------------------------
// Music mood and alerts (1000:07A1..0842)
// ---------------------------------------------------------------------------

// The DS:45CE == 2 (Roland/MT-32 bank) test of mus_get_mood and
// mis_start_extraction is omitted: engine/sound.cpp fixes the bank at 1.
int musGetMood() {
    const MissionState& S = ms();
    if (S.time < S.contactUntil) return 2;
    if (S.time < S.alertUntil) return 1;
    return 0;
}

void musUpdateMood() {
    // g_music_current (DS:EFD8) is kept by the sound layer (also set by the
    // death music of the casualty handlers).
    const int current = engine::sound().currentTrack();
    if (engine::paletteFade().level() != 0 || current >= 6) return;
    const int mood = musGetMood();
    if (current == 3 || current == 4) {
        if (!engine::sound().done()) return;
    } else {
        if (mood == current || mood < 0 || mood > 2) return;
    }
    stopMusic();
    playMusic(mood);
}

void misCheckAlerts() {
    MissionState& S = ms();
    if (S.alertUntil <= S.time && entTeamEnemySighted()) {
        if (S.contactUntil <= S.time) {
            msgShowDs(0x0271, 0x100);  // "Enemy sighted."
            if (wrapAdd(S.alertUntil, 0x2800) <= S.time) msgHandSignal(pointMan(), 2);
        }
        S.alertUntil = wrapAdd(S.time, 0x1400);
    }
    if (S.radioDamaged && S.radioRepairTime < S.time) {
        msgShowDs(0x0280, 0x200);  // "Radio repaired."
        S.radioDamaged = false;
    }
}

// ---------------------------------------------------------------------------
// Mission event tick (2dbd:3F4C)
// ---------------------------------------------------------------------------

void evtMissionTick() {
    MissionState& S = ms();
    evtUpdateSupportCraft();
    if (elapsedPeriod(S.minuteJobTime, 0x3C00)) evtPlayCraftEngineSounds();
    if (elapsedPeriod(S.job4sTime, 0x400)) evtSplitTeamsUpdate();
    evtUpdateExtractionPickup();
    evtExecuteAiCommands();
    evtAnnounceMedicAssignments();
    evtUpdateUnitMovement();
    for (int i = 0; i < kMaxTeams && S.teams[i]; ++i) evtTeamFormationUpdate(i);
    for (Projectile* p : S.projectiles)
        if (p && (p->state & prj_state::kInUse)) evtUpdateOrdnance(p);
    modernPhantomUpdate();  // port: Modern gameplay Phantom flight (no-op while off)
    for (Obj3D* o : fxPools().trees)
        if (o && (o->flags & obj3d_flag::kEnabled)) evtUpdateAmbientFlyer(o);
}

// ---------------------------------------------------------------------------
// Set-up and frame schedule (1000:01DE)
// ---------------------------------------------------------------------------

bool simInit(const MissionSetup& setup) {
    loadGameData();
    resetMission();
    MissionState& S = ms();
    S.missionNo = setup.missionNo;
    S.year = setup.year;
    S.gameMode = setup.gameMode;
    S.opt = setup.options;
    S.loadout = setup.loadout;
    S.detailLevel = setup.detailLevel;
    S.handSignalGfx = setup.handSignals;
    setRosterLookup(setup.rosterFind);
    engine::sound().setMuted(setup.headless);

    S.quitGame = false;
    S.misDone = false;
    S.tcState = 0;
    S.extractionEnd = 0;
    if (!msnLoad(setup.missionNo)) return false;
    S.misPrisoners = 0;
    S.misDocuments = 0;
    S.misWeapons = 0;
    // clk_reset_start_timer + clk_reset
    S.time = 0;
    S.prevTime = 0;
    S.frameTicks = 1;
    engine::sound().setGameClock(0, false);
    engine::sound().resetChannels();
    engine::sound().loadSfxBank();
    // mis_load_world (1000:4580)
    worldBegin();
    wldLoad(S.mci.world);
    fxCreatePools();
    msnBuildWorld();
    worldEnd();
    misScanSpecials();

    todSet(S.mci.start_hour & 0xFF, S.mci.start_minute & 0xFF);
    mapInitMarkers();
    entUnitFaceObjective(pointMan());
    evtTeamSnapFormation(0);
    S.viewMode = 0;
    prjResetAll();
    S.aiEnabled = true;
    aiInit();
    S.stats = MissionStats{};  // 365e:DBE6 score_reset_stats (team size is kept by the front end)
    campaign::scoreResetStats();  // the campaign-side copy the debriefing reads
    // hud_reset_mission (1000:1EB0) -> view_set_first_person (1000:1ED2): veg_update(1), mode 0.
    setViewMode(0);
    hudShowWeaponLines();
    // hud_reset_mission (1000:1EB0): simulation timers; the views are the loop's.
    S.demoExplodeTime = 0;
    S.toolInfoTime = 0;
    S.grenadeInfoTime = 0;
    S.grenadeReadyTime = 0;
    S.weaponReadyTime = 0;
    S.weaponInfoTime = 0;
    S.radioDamaged = false;
    S.radioRepairTime = 0;
    if (Unit* pm = pointMan())
        team(0)->view_heading = s16(mathHeading(S.wpSeal.x, S.wpSeal.z, pm->body->pos.x, pm->body->pos.z));
    // clk_reset; all periodic timestamps = g_time.
    S.time = 0;
    S.prevTime = 0;
    S.frameTicks = 1;
    S.tUnits = S.t9s = S.t4s = S.tAi = S.fatigueTick = S.job4sTime = S.minuteJobTime = S.teamMoveTick =
        S.diveReadyTime = S.searchTick = S.teamIdleTick = S.alertUntil = S.contactUntil = S.autotargetTime =
            S.teamFireTime = S.time;
    S.extracting = false;
    msgQueueReset();
    misStartInsertion();
    // field_view_keys(0x32, 0, 0): the Point Man starts crouched.
    playerViewPrologue(0);
    playerSetPosture(1);
    if (g_vegResetHook) g_vegResetHook();  // veg_reset (1000:70B3)
    if (g_vegHook) g_vegHook(true);        // veg_update(1)
    evtPlayCraftEngineSounds();
    tgtClear();
    return true;
}

void simShutdown() {
    copyMissionStats();  // also after an aborted mission
    aiShutdown();
    engine::sound().unloadSfxBank();
    worldFree();
}

void simFrameClock(Ticks now) {
    MissionState& S = ms();
    S.time = now;
    // 1000:302D..303E: 16-bit low-word difference, then <= 0 becomes 1.
    const s16 d = s16(u16(S.time) - u16(S.prevTime));
    S.frameTicks = d > 0 ? d : 1;
    S.prevTime = S.time;
    engine::sound().setGameClock(S.time, S.tcState != 0);
}

void simFrameClockCompressed() { simFrameClock(wrapAdd(ms().time, 0x80)); }

void simFrameAnimate() {
    todUpdate();
    sprAdvanceAnimClocks();
}

int simFrameUpdate() {
    MissionState& S = ms();
    S.losCalls = 0;
    if (S.extractionEnd != 0 && S.time < S.extractionEnd && wrapSub(S.extractionEnd, S.time) < 0x100) S.misDone = true;
    if (S.viewMode == 0x0C) return -1;
    S.elapsed = s16(wrapSub(S.time, S.tUnits));
    if (elapsedPeriod(S.tUnits, 0x40)) shotUpdateAll(S.elapsed);
    S.elapsed = s16(wrapSub(S.time, S.tAi));
    if (elapsedPeriod(S.tAi, 0x140)) {
        if (S.aiEnabled) aiUpdate(S.elapsed);
        if (S.tcState == 0) musUpdateMood();
        misCheckAlerts();
    }
    S.elapsed = s16(wrapSub(S.time, S.t4s));
    if (elapsedPeriod(S.t4s, 0x400)) {
        noiseUpdate();
        medBleedTick(S.elapsed);
        msnCheckObjectives();
    }
    evtMissionTick();
    if (elapsedPeriod(S.t9s, 0x900) && g_vegHook) g_vegHook(true);
    const bool fading = S.extractionEnd != 0 && S.time < S.extractionEnd && wrapSub(S.extractionEnd, S.time) < 0x400;
    return fading ? 0x700 : 0;
}

void simMsgTick() { msgQueueTick(); }

// ---------------------------------------------------------------------------
// Player actions
// ---------------------------------------------------------------------------

void playerSetPosture(int posture, bool fromPointer) {
    Unit* pm = pointMan();
    if (!pm) return;
    // Original quirk (19ac:1983): the pointer path has no death check, only
    // the '1'/'2'/'3'/'+'/'-' keys refuse for a dead Point Man.
    if (!fromPointer && unitDead(pm)) return;
    evtSetPosture(pm, posture);
    sprSetAnim(pm, u8(pm->mover->posture));
    msgShow(dsTextPtr(u16(kPostureNames + 2 * (u8(pm->mover->posture) & 3))), 0x100);
}

void playerTurn(int d) { unitTurn(pointMan(), d); }

bool playerOrderKey(int key) {
    MissionState& S = ms();
    Team* t0 = team(0);
    Unit* pm = pointMan();
    Team* sel = team(S.mapSelTeam);
    const bool main = S.mapSelTeam == S.sealTeam || S.viewMode != 1;
    auto abortSearch = [&](Team* t, u16 msgOff) {
        if (t->order == s16(TeamOrder::Search)) {
            msgShowDs(msgOff, 0x100);
            evtSearchEndReport();
        }
    };
    auto setMode = [&](Team* t, int m) {
        if (unitAlive(t->members[0])) evtSetMoveMode(t->members[0], m);
    };
    switch (key) {
    case 'c':
        if (main) {
            if (t0->fire_order == FireOrder::CeaseFire) msgHandSignal(pm, 12);
            else msgShowDs(0x461, 0x100);
            sel = t0;
        } else if (sel->type == TeamType::Seal) {
            radioCall(dsText(0x46D), S.mapSelTeam);
        }
        sel->fire_order = FireOrder::CeaseFire;
        return false;
    case 'd':
        if (!main) {
            if (sel->type == TeamType::Seal) {
                radioCall(dsText(0x4A9), S.mapSelTeam);
                sel->fire_order = FireOrder::Demolish;
            }
            return false;
        }
        t0->formation = Formation::Diamond;
        msgHandSignal(pm, 11);
        return false;
    case 'f':
        if (main) {
            msgHandSignal(pm, 14);
            sel = t0;
        } else if (sel->type == TeamType::Seal) {
            radioCall(dsText(0x441), S.mapSelTeam);
        }
        sel->fire_order = FireOrder::FieldOfFire;
        return false;
    case 'h':
        if (!main) {
            if (sel->type != TeamType::Seal) return false;
            abortSearch(sel, 0x334);
            sel->order = 0;
            setMode(sel, 0);
            radioCall(dsText(0x350), S.mapSelTeam);
            return false;
        }
        abortSearch(t0, 0x318);
        t0->order = 0;
        evtPlayerSpeedStep(0);
        msgHandSignal(pm, 0);
        return false;
    case 'i':
        if (!main) {
            if (sel->type == TeamType::Seal) {
                radioCall(dsText(0x49C), S.mapSelTeam);
                sel->fire_order = FireOrder::Snipe;
            }
            return false;
        }
        t0->formation = Formation::InLine;
        msgHandSignal(pm, 9);
        return false;
    case 'j': {
        // Port (Modern gameplay): with a craft or the Phantom flight selected the
        // original only falls back to team 0 below, so the join took a second
        // press; the teams now join at once.
        if (modernGameplayOn() && S.splitGroups != 0 && sel && sel->type != TeamType::Seal) S.mapSelTeam = S.sealTeam;
        int r;
        if (S.mapSelTeam == S.sealTeam) {
            r = entGroupMerge(0, S.teamCount - 1);
            if (r != 0) {
                S.mapSelTeam = 0;
                return false;
            }
            msgShowDs(0x413, 0x200);  // "Team joined."
        } else {
            if (sel->type != TeamType::Seal) {
                S.mapSelTeam = 0;
                return false;
            }
            abortSearch(sel, 0x420);
            r = entGroupMerge(0, S.teamCount - 1);
            if (r != 0) {
                S.mapSelTeam = 0;
                return false;
            }
            radioCall(dsText(0x43C), S.mapSelTeam);
        }
        if (S.viewMode > 0x10) {
            setViewMode(2);
            hudShowWeaponLines();
        }
        S.mapSelTeam = 0;
        return false;
    }
    case 'l':
        if (!main) {
            if (sel->type != TeamType::Seal) return false;
            abortSearch(sel, 0x478);
            sel->order = s16(TeamOrder::Stealth);
            setMode(sel, 1);
            radioCall(dsText(0x494), S.mapSelTeam);
            return false;
        }
        t0->formation = Formation::Column;
        msgHandSignal(pm, 8);
        return false;
    case 'p':
        if (S.mapSelTeam == S.sealTeam) {
            const int n = entGroupSplit(0);
            if (n > 0) {
                Team* t = team(n);
                t->fire_order = FireOrder::AtWill;
                t->formation = Formation::Column;
                t->order = 0;
                setMode(t, 0);
                msgShowDs(0x3DA, 0x200);  // "Team split."
                if (S.viewMode == 1) S.mapSelTeam = S.teamCount - 1;
                evtSplitTeamsUpdate();
            }
            return false;
        }
        if (sel->type != TeamType::Seal) return false;
        abortSearch(sel, 0x3E6);
        sel->order = s16(TeamOrder::Asap);
        setMode(sel, 2);
        radioCall(dsText(0x402), S.mapSelTeam);
        return false;
    case 'q':
        if (S.time <= S.diveReadyTime) return true;
        evtTeamDiveQuickly(t0);
        S.diveReadyTime = wrapAdd(S.time, 0x400);
        return true;
    case 'r':
        if (S.viewMode == 7 || S.viewMode == 0x0C) return false;
        if (pm && pm->loadout && pm->loadout->primary)  // port guard; the original assumes a weapon
            pm->loadout->primary->fire_mode = u8(wpnNextRof(pm->loadout->primary));
        S.weaponInfoTime = wrapAdd(S.time, 0x100);
        return false;
    case 's':
        if (main) {
            msgHandSignal(pm, 6);
            if (grpCountAlive(t0) < 2) {
                msgShowDs(0x355, 0x200);
                return false;
            }
            if (evtFindNearestSearchable(pm, false) != 300) {
                if (!evtSearchBegin(t0)) msgShowDs(0x388, 0x200);
                return false;
            }
            msgShowDs(0x375, 0x200);
            msgHandSignal(pm->team->members[1], 7);
            return false;
        }
        if (sel->type != TeamType::Seal) return false;
        if (evtFindNearestSearchable(sel->members[0], false) != 300) {
            // Original: the search is always started for team 0.
            if (evtSearchBegin(t0)) radioCall(dsText(0x3D3), S.mapSelTeam);
            else msgShowDs(0x3B7, 0x200);
            return false;
        }
        msgShowDs(0x3A4, 0x200);
        msgHandSignal(sel->members[0], 7);
        return false;
    case 't':
        if (main) {
            msgHandSignal(pm, 15);
            sel = t0;
        } else if (sel->type == TeamType::Seal) {
            radioCall(dsText(0x44F), S.mapSelTeam);
        }
        sel->fire_order = FireOrder::AtTarget;
        return false;
    case 'v':
        if (!main) {
            if (sel->type != TeamType::Seal) return false;
            sel->fire_order = FireOrder::CoverFire;
            radioCall(dsText(0x4B9), S.mapSelTeam);
            return false;
        }
        t0->formation = Formation::VeeWedge;
        msgHandSignal(pm, 10);
        return false;
    case 'w':
        if (main) {
            msgHandSignal(pm, 13);
            sel = t0;
        } else if (sel->type == TeamType::Seal) {
            radioCall(dsText(0x459), S.mapSelTeam);
        }
        sel->fire_order = FireOrder::AtWill;
        return false;
    case 0x1400:  // Alt-T time compression
        if (S.tcState == 0) {
            sfxStopAll();
            stopMusic();
            playMusic(5);
            tcOn();
        } else {
            tcOff();
            stopMusic();
            msgShowDs(0x4C4, 0x100);
        }
        return true;
    case 0x1600:  // Alt-U auto-target
        S.autoTarget = !S.autoTarget;
        msgShowDs(S.autoTarget ? 0x2F9 : 0x308, 0x100);
        return true;
    default:
        return false;
    }
}

} // namespace st::game::mission
