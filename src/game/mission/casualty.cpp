// Casualties and weapon-handling animations of segment 2dbd part 1: helper
// (buddy) assignment, prisoner escape, emergency extraction, the killed /
// bled-to-death / wounded / suppressed handlers called by the damage code,
// and the reload / launcher / demolition crouches. docs/re/seg_2dbd.md 3.2-3.4.
#include "game/mission/people.h"

#include "data/exeimage.h"
#include "engine/palette_fade.h"
#include "engine/rng.h"
#include "engine/sound.h"
#include "engine/ticker.h"
#include "game/mission/build.h"
#include "game/mission/combat.h"
#include "game/mission/entity.h"
#include "game/mission/exedata.h"
#include "game/mission/geo.h"
#include "game/mission/msg.h"
#include "game/mission/sfx.h"
#include "game/mission/state.h"

#include <string>

namespace st::game::mission {

namespace {

constexpr u16 kHelperSlots = 0x089C;  // s16[4] team slots tried for a helper: 2, 1, 3, 0
constexpr u16 kRoleNames = 0x1C8C;    // char*[] "Point Man", "Officer-in-Charge", ... by team slot

// Texts (DS offsets). The "<role> <name> ..." lines are built by strcat
// from these pieces exactly like the original.
constexpr u16 kMsgPrisonerEscaped = 0x08AC;  // "A prisoner has escaped."
constexpr u16 kMsgExtracting = 0x08C4;       // "Extracting!"
// evt_unit_killed
constexpr u16 kKilledThe = 0x08D0;            // "The "
constexpr u16 kKilledEmpty = 0x08D5;          // ""
constexpr u16 kKilledSpace = 0x08D6;          // " "
constexpr u16 kKilledIsDead = 0x08D8;         // " is dead."
constexpr u16 kKilledCampaignEnded = 0x08E2;  // "Campaign Ended"
constexpr u16 kKilledPickHimUp = 0x08F1;      // "Pick him up."
// evt_unit_bled_to_death
constexpr u16 kBledThe = 0x08FE;
constexpr u16 kBledEmpty = 0x0903;
constexpr u16 kBledSpace = 0x0904;
constexpr u16 kBledIsDead = 0x0906;
constexpr u16 kBledCampaignEnded = 0x0910;
constexpr u16 kBledPickHimUp = 0x091F;
// evt_unit_wounded
constexpr u16 kWoundEmpty = 0x092C;           // ""
constexpr u16 kWoundSpace = 0x092D;           // " "
constexpr u16 kWoundBleeding = 0x092F;        // " is bleeding heavily!"
constexpr u16 kWoundHeavy = 0x0945;           // " is heavily wounded!"
constexpr u16 kWoundNeedsHelp = 0x095A;       // "He needs help."
constexpr u16 kHitEmpty = 0x0969;             // ""
constexpr u16 kHitSpace = 0x096A;             // " "
constexpr u16 kHitHasBeenHit = 0x096C;        // " has been hit!"
constexpr u16 kPmThe = 0x097B;                // "The "
constexpr u16 kPmBleeding = 0x0980;           // " is bleeding heavily!"
constexpr u16 kPmHeavy = 0x0996;              // " is heavily wounded."
constexpr u16 kPmWham = 0x09AB;               // "Wham!"
// evt_unit_suppressed
constexpr u16 kMsgSuppressed = 0x09B1;        // "Suppressed!"
// evt_start_reload
constexpr u16 kReloadEmpty = 0x09BD;          // ""
constexpr u16 kReloadSpace = 0x09BE;          // " "
constexpr u16 kReloadColon = 0x09C0;          // ": "
constexpr u16 kReloadOne = 0x09C3;            // "\"One reload remaining.\""
constexpr u16 kReloadOut = 0x09DB;            // "\"Out of reloads!\""

// Role name of the unit's team slot (DS:1C8C[slot]; slot -1 reads the word before the table like the original).
std::string roleOf(const Unit* u) { return dsTextPtr(u16(kRoleNames + 2 * unitIndexInGroup(u))); }

bool isTeam0(const Unit* u) { return u->team == ms().teams[0]; }

// Hand signal 4 "Under Fire" by the Point Man unless one is still running.
void underFireSignal() {
    if (ms().contactUntil < ms().time) msgHandSignal(pointMan(), int(HandSignal::UnderFire));
}

// Every team-0 casualty: under-fire time DS:0898 = now + 0xF00, then the escort check.
void markContact(Unit* u) {
    ms().contactUntil = wrapAdd(ms().time, 0xF00);
    evtCheckPrisonerEscape(u);
}

// buddy = helper from the Point Man's team, linked both ways.
void linkTeam0Helper(Unit* u) {
    Unit* h = evtAssignHelper(u, pointMan()->team);
    u->buddy = h;
    if (h) h->buddy = u;
}

void playDeathMusic(int track) {
    engine::sound().stop();
    engine::sound().play(track);
}

// The Point Man's mover flag 0x40 (aboard the extraction craft) keeps the music.
bool musicFree() { return !(pointMan()->mover->flags & mover_flag::kAboard); }

// Point Man death: campaign over, extraction at his position, black palette.
void pointManDied(Unit* u, const std::string& text, u16 campaignEnded, bool vertical) {
    Team* t0 = ms().teams[0];
    msgShow(text, 0xA00);
    msgShowDs(campaignEnded, 0x5A00);
    t0->order = 0;
    entOrderExtraction(u->body->pos);
    unit1UseTool();
    if (vertical) engine::ticker().shakeVertical(0x55);
    else engine::ticker().shakeHorizontal(0x33);
    engine::paletteFade().setLevel(0x100);
    t0->fire_order = FireOrder::AtWill;
}

} // namespace

// ---------------------------------------------------------------------------
// Helper assignment and follow-up checks
// ---------------------------------------------------------------------------

// 2dbd:004E. The callers set both buddy links.
Unit* evtAssignHelper(Unit* u, Team* t) {
    if (!u || !t) return nullptr;
    if (u->mover->flags & mover_flag::kEscort) {
        // The unit was itself helping someone: that patient gets a new helper.
        Unit* patient = u->buddy;
        if (patient) {
            patient->buddy = nullptr;
            patient->buddy = evtAssignHelper(patient, t);
            if (patient->buddy) patient->buddy->buddy = patient;
        }
        u->buddy = nullptr;
        u->mover->flags &= 0x7F;
    }
    if (u->buddy) return u->buddy;
    for (int k = 0; k < 4; ++k) {
        const int slot = exe().dgShort(u16(kHelperSlots + 2 * k));
        Unit* c = (slot >= 0 && slot < 8) ? t->members[slot] : nullptr;
        if (c && !(c->status->hit_mask & hit_bit::kKilled) && c->status->heavy_wounds == 0 && !c->buddy && c != u) {
            c->mover->flags |= mover_flag::kEscort;
            return c;
        }
    }
    return nullptr;
}

// 2dbd:0520: a hit SEAL escort may lose his prisoner.
void evtCheckPrisonerEscape(Unit* u) {
    if (!u) return;
    Unit* p = u->buddy;
    if (!p) return;
    if (u->team->type != TeamType::Seal) return;
    if (!(u->mover->flags & mover_flag::kEscort)) return;
    if (!(p->mover->flags & mover_flag::kSecured)) return;
    if (!isEnemyTeam(p->team)) return;
    bool escape = false;
    if (invCountItems(u, int(ItemType::Phk)) == 0 && engine::rng().range(100) < 50) escape = true;
    else if (unitDead(u)) escape = true;
    if (!escape) return;
    p->buddy = nullptr;
    p->mover->flags &= 0xDF;
    if (p->brain) p->brain->surrendered = 0;
    evtSetMoveMode(p, 2);
    u->buddy = nullptr;
    u->mover->flags &= 0x7F;
    msgShowDs(kMsgPrisonerEscaped, 0x400);
    ms().misPrisoners = s16(ms().misPrisoners - 1);
}

// 2dbd:0632
void evtCheckEmergencyExtraction(Team* t) {
    if (!t || t->type != TeamType::Seal) return;
    if (entAnyCraftMoving()) return;
    if (sealCountAlive() > 2) return;
    entOrderExtraction(pointMan()->body->pos);
    unit1UseTool();
    msgShowDs(kMsgExtracting, 0x400);
}

// ---------------------------------------------------------------------------
// Casualty handlers (called by dmg_apply 1000:521C and the bleed tick 19ac:89C3)
// ---------------------------------------------------------------------------

// 2dbd:0684
void evtUnitKilled(Unit* u) {
    if (u->anim && u->anim->return_state == AnimState::Dead) return;
    evtSetMoveMode(u, 0);
    u->mover->speed = u->mover->target_speed;
    if (u->anim) u->anim->move_mode = 0;
    sprSetAnim(u, 3);
    evtSetPosture(u, 2);
    if (engine::rng().range(6) == 0) {
        const int type = int(u->team->type);
        sfxPlay(type == 0 || type == 7 ? 0x34 : 0x26, 0x300, &u->body->pos, 1, u);
    }
    if (isTeam0(u)) {
        const bool pm = u == pointMan();
        std::string text;
        if (pm) {
            text = dsText(kKilledThe) + roleOf(u);
            msgQueueClear();
        } else {
            text = dsText(kKilledEmpty) + roleOf(u) + dsText(kKilledSpace) + unitName(u);
        }
        text += dsText(kKilledIsDead);
        if (pm) {
            pointManDied(u, text, kKilledCampaignEnded, false);
        } else {
            msgShow(text, 0x200);
            linkTeam0Helper(u);
            if (u->buddy && u->buddy == pointMan()) msgShowDs(kKilledPickHimUp, 0x200);
            underFireSignal();
            if (musicFree()) playDeathMusic(3);
        }
        markContact(u);
    } else if (u->team->type != TeamType::Seal) {
        entGroupReplaceLeader(u->team, u);
        if (grpCountAlive(u->team) == 0 && isEnemyTeam(u->team) && musicFree()) playDeathMusic(4);
    }
    evtCheckEmergencyExtraction(u->team);
}

// 2dbd:0A9D: as killed, without scream, music, contact time or speed reset.
void evtUnitBledToDeath(Unit* u) {
    if (u->anim && u->anim->return_state == AnimState::Dead) return;
    evtSetMoveMode(u, 0);
    sprSetAnim(u, 3);
    evtSetPosture(u, 2);
    if (isTeam0(u)) {
        const bool pm = u == pointMan();
        std::string text;
        if (pm) {
            text = dsText(kBledThe) + roleOf(u);
            msgQueueClear();
        } else {
            text = dsText(kBledEmpty) + roleOf(u) + dsText(kBledSpace) + unitName(u);
        }
        text += dsText(kBledIsDead);
        if (pm) {
            pointManDied(u, text, kBledCampaignEnded, true);
        } else {
            linkTeam0Helper(u);
            msgShow(text, 0x200);
            if (u->buddy && u->buddy == pointMan()) msgShowDs(kBledPickHimUp, 0x200);
        }
        evtCheckPrisonerEscape(u);
    } else if (u->team->type != TeamType::Seal) {
        entGroupReplaceLeader(u->team, u);
    }
    evtCheckEmergencyExtraction(u->team);
}

// 2dbd:0D6F
void evtUnitWounded(Unit* u) {
    if (s16(u->status->unit_class) >= 5) return;  // craft, civilians, friendlies
    Mover* m = u->mover;
    evtSetMoveMode(u, 0);
    if (u->anim) u->anim->move_mode = 0;
    m->desired_heading = m->impact_bearing;  // face the hit
    evtSetPosture(u, 2);
    sprSetAnim(u, 2);
    const int type = int(u->team->type);
    if (engine::rng().range(6) == 0) {
        sfxPlay(type == 0 || type == 7 ? 0x35 : 0x27, 0x200, &u->body->pos, 1, u);
    } else if (engine::rng().range(5) == 0 && (type == 4 || type == 5)) {
        sfxPlay(0x29, 0x200, &u->body->pos, 1, u);
    }
    const Status* s = u->status;
    if (!isTeam0(u)) {
        // Split SEAL teams: a seriously wounded member gets a helper of his own team.
        if (type != 0 || unitDead(u) || (u->mover->flags & mover_flag::kSecured) || !s->heavy_wounds) return;
        Unit* h = evtAssignHelper(u, u->team);
        u->buddy = h;
        if (h) h->buddy = u;
        return;
    }
    if (unitDead(u) || u == pointMan()) {
        if (!unitDead(u) && u == pointMan()) {
            if (!s->heavy_wounds) {
                msgShowDs(kPmWham, 0x100);
            } else {
                msgShow(dsText(kPmThe) + roleOf(u) + dsText(s->bleeding ? kPmBleeding : kPmHeavy), 0x200);
            }
            engine::ticker().shakeHorizontal(0x2A);
            engine::paletteFade().setLevel(-0x100);  // red flash
        }
    } else {
        if (!s->heavy_wounds) {
            msgShow(dsText(kHitEmpty) + roleOf(u) + dsText(kHitSpace) + unitName(u) + dsText(kHitHasBeenHit), 0x200);
        } else {
            msgShow(dsText(kWoundEmpty) + roleOf(u) + dsText(kWoundSpace) + unitName(u) +
                        dsText(s->bleeding ? kWoundBleeding : kWoundHeavy),
                    0x200);
            linkTeam0Helper(u);
            if (u->buddy && u->buddy == pointMan()) msgShowDs(kWoundNeedsHelp, 0x200);
        }
        underFireSignal();
    }
    markContact(u);
}

// 2dbd:12E7: near miss.
void evtUnitSuppressed(Unit* u) {
    evtSetMoveMode(u, 0);
    if (u->anim) u->anim->move_mode = 0;
    evtSetPosture(u, 2);
    sprSetAnim(u, 2);
    if (!isTeam0(u)) return;
    if (u == pointMan()) {
        msgShowDs(kMsgSuppressed, 0x100);
        engine::ticker().shakeVertical(0x2A);
    } else {
        underFireSignal();
    }
    markContact(u);
}

// ---------------------------------------------------------------------------
// Weapons handling animations
// ---------------------------------------------------------------------------

// 2dbd:13C1: firearms only (not rockets/launchers 0x0F/0x10, not the satchel).
void evtStartReload(Unit* u) {
    if (!u || !u->loadout || !u->loadout->primary) return;
    const WeaponNode* w = u->loadout->primary;
    const int cls = int(weaponDef(u8(w->type)).wclass);
    if (cls == 0x10 || cls == 0x0F || u8(w->type) == 0x0C || w->reloads == 0) return;
    if (u->anim) {
        evtSetMoveMode(u, 0);
        u->anim->move_mode = 0;
        evtSetPosture(u, 1);
        sprSetAnim(u, 1);
        sprSetAnim(u, 16);  // crouch reload
    }
    if (!isTeam0(u)) return;
    if (u16(w->reloads) >= 1) sfxPlay(0x0D, 0x100, &u->body->pos, 1, nullptr);
    if (u == pointMan()) return;
    if (u16(w->reloads) > 2) return;
    msgShow(dsText(kReloadEmpty) + roleOf(u) + dsText(kReloadSpace) + unitName(u) + dsText(kReloadColon) +
                dsText(w->reloads == 2 ? kReloadOne : kReloadOut),
            0x200);
}

// 2dbd:161E: before firing weapon classes 0x0F/0x10 (crouch, shoulder fire).
void evtCrouchToFireLauncher(Unit* u) {
    if (!u || !u->anim) return;
    evtSetMoveMode(u, 0);
    u->anim->move_mode = 0;
    evtSetPosture(u, 1);
    sprSetAnim(u, 1);
    sprSetAnim(u, 17);
}

// 2dbd:168B: before placing a demolition charge.
void evtCrouchToPlaceCharge(Unit* u) {
    if (!u || !u->anim) return;
    evtSetMoveMode(u, 0);
    u->anim->move_mode = 0;
    evtSetPosture(u, 1);
    sprSetAnim(u, 1);
}

} // namespace st::game::mission
