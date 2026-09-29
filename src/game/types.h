// Canonical definitions of SEAL Team's run-time records and data file formats.
//
// One struct per original record, one field per known member, in the original
// order. The trailing comment of every field gives its byte offset in the
// original record and its meaning. Unknown members keep a placeholder name
// (unk_XX = offset) so offsets stay traceable. These are run-time structs for
// readable game code, not memory images: far pointers are typed C++ pointers,
// near pointers into DGROUP tables (model descriptors, strings) as well.
//
// The on-disk formats (MCI, MTM, SE, .cmp campaign + roster, s.cnf, st1.dfr,
// .S text, .W/.WD world) get explicit parse/serialize functions (declared at
// the end) that read and write the exact original byte layout, so saves stay
// compatible with the original game.
//
// Evidence, per-field readers/writers and the resolution of conflicting notes
// are in docs/re/STRUCTURES.md. Conventions:
//   * time: 1 tick = 1/256 s (g_time, DS:ECA6)
//   * Angle8: 1/8 degree, 0..2879 (0xB40 = 360 degrees); "deg" = whole degrees
//   * Vec3: 3 x s32, 24.8 fixed point world position, y = altitude (up)
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "core/common.h"

namespace st::game {

using Ticks = s32;   // 1/256 s
using Angle8 = s16;  // 1/8 degree, 0..2879

struct Vec3 {
    s32 x;  // +0x00 east-west, 24.8 (integer part = map units)
    s32 y;  // +0x04 altitude (up), 24.8
    s32 z;  // +0x08 north-south, 24.8
};

struct Obj3D;
struct ModelDesc;
struct WorldObject;
struct Unit;
struct Status;
struct Mover;
struct Anim;
struct Brain;
struct Loadout;
struct WeaponNode;
struct ItemNode;
struct Team;
struct TeamAi;
struct WaypointNode;
struct ScriptStep;
struct Projectile;
struct FlightRec;
struct SeRecord;
struct RosterEntry;
struct SpriteFrame;

// ===========================================================================
// Enumerations
// ===========================================================================

// Top-level game mode g_game_mode (DS:D7E4). game/globals.h currently declares
// the same values as GameMode; this copy uses another name to avoid a clash.
enum class GameMode : s16 {
    Campaign = 1,  // continue a campaign (also 2 -> 1 after the first debriefing)
    Menu = 2,      // main menu / start a new campaign
    Practice = 3,  // practice mission
    Demo = 6,      // single mission from the command line, then exit
};

// View mode g_view_mode (DS:D844).
enum class ViewMode : s16 {
    FirstPerson = 0x00,   // F1, Point Man's eyes (main camera DS:D84E)
    Map = 0x01,           // map / control panel (map camera DS:D86E)
    Chase = 0x02,         // F2, orbit behind the Point Man's team (small camera DS:D88A)
    Support1 = 0x03,      // F3, team g_insertion_group+0
    Support2 = 0x04,      // F4, team g_insertion_group+1
    CampInsertion = 0x05, // SEAL camp cut-scene before the mission (orbit team ec8c)
    CampExtraction = 0x06,// SEAL camp cut-scene after the mission (orbit team ec8d)
    Insertion = 0x07,     // insertion orbit at mission start
    Extraction = 0x08,    // extraction orbit
    Reinsert = 0x0C,      // map camera at the re-insertion point ('R' during insertion)
    Target = 0x0D,        // F9, team of the current target
    Enemy = 0x0E,         // F10, cycles enemy teams (game modes 3 and 6 only)
    Support3 = 0x0F,      // F5, team g_insertion_group+2
    Support4 = 0x10,      // F6, team g_insertion_group+3
    SplitA = 0x11,        // F7, first split SEAL team
    SplitB = 0x12,        // F8, second split SEAL team
};

// Team (group) type, Team::type (+0x26). Names at DS:1C7C.
enum class TeamType : s16 {
    Seal = 0,        // SEAL Team (group 0 = the player's squad)
    Boat = 1,        // Boat Support Unit (LSSC / Mike boat)
    Helicopter = 2,  // Seawolf Helicopter (UH-1B)
    Aircraft = 3,    // Bronco Aircraft (OV-10 "Black Pony")
    VietCong = 4,
    NvArmy = 5,
    Civilian = 6,
    Friendly = 7,    // rescued/allied NPCs (FRNDnn.se)
};

// Team formation, Team::formation (+0x20). Keys l/i/d/v, hand signals 8..11.
enum class Formation : s16 {
    Column = 0,
    InLine = 1,
    Diamond = 2,
    VeeWedge = 3,
};

// Team fire order, Team::fire_order (+0x22). 4..6 only for split SEAL teams.
enum class FireOrder : s16 {
    FieldOfFire = 0,
    AtTarget = 1,
    AtWill = 2,      // also forced on team 0 when the Point Man dies
    CeaseFire = 3,
    CoverFire = 4,
    Demolish = 5,
    Snipe = 6,
};

// Team::order (+0x24) for foot teams (SEAL, MTM groups).
enum class TeamOrder : s16 {
    None = -1,    // SEAL team at creation
    Halt = 0,     // also the value of every MTM group
    Asap = 1,     // split team: run to the rally point
    Stealth = 2,  // split team: crawl to the rally point
    Search = 4,   // searching bodies (only one team at a time)
};

// Team::order (+0x24) for support craft (TeamType 1..3). The craft update
// (2dbd:297d) treats 0 and 1 like 2 and 3.
enum class CraftOrder : s16 {
    Parked = 0,            // camp scenes (grp_set_craft_status(g, 0))
    Extract = 2,           // 'e' on the map
    EmergencyExtract = 3,  // 'y' on the map, or automatic (ent_order_extraction)
    Loiter = 4,            // initial state of every craft
    Attack = 5,
    CeaseAttack = 6,       // also set when out of ammunition
};

// Mover::move_mode (+0x24). Names at DS:1D9C.
enum class MoveMode : u8 {
    Stop = 0,
    Slow = 1,  // target speed = v/4
    Run = 2,   // target speed = v (forces people upright)
    Back = 3,  // target speed = -v/4
};

// Mover::posture (+0x25, soldiers) and Anim::posture (3 = dead).
enum class Posture : u8 {
    Upright = 0,
    Crouch = 1,
    Prone = 2,
    Dead = 3,  // only in Anim / UI
};

// Mover::posture (+0x25) re-used by craft as altitude mode (2dbd:2841).
enum class AltitudeMode : u8 {
    High = 0,    // target height = mover.high
    Medium = 1,  // (high >> 1) + low
    Low = 2,     // mover.low
};

// Unit class, argument of the spawner and Status::unit_class (+0x1A).
enum class UnitClass : s16 {
    Seal = 0,
    VietCong = 3,
    NvArmy = 4,
    Boat = 5,
    Helicopter = 6,
    Aircraft = 7,
    Civilian = 8,
    Friendly = 9,
};

// Status::skill index = SE attributes 0..7 (names DS:24EC).
enum class Skill : u8 {
    Rifle = 0, Pistol = 1, Mortar = 2, Shoulder = 3, Automatic = 4, Throw = 5, Observe = 6, Radio = 7,
};

// Objective kind, MciObjective::kind. Names at DS:1D72.
enum class ObjectiveKind : u16 {
    None = 0, Patrol = 1, Ambush = 2, Demolition = 3, Observe = 4, Rescue = 5, Snatch = 6, Recover = 7,
};

// MTM team kind, MtmTeam::kind -> (TeamType, UnitClass).
enum class MtmKind : u16 {
    Civilian = 0,  // (6, 8)
    VietCong = 1,  // (4, 3); also every value other than 0/2/3
    NvArmy = 2,    // (5, 4)
    Friendly = 3,  // (7, 9)
};

// Insertion/extraction method, MCI +0x06/+0x08. Names DS:25C0[v & ~1].
enum class InsertionMethod : u16 {
    Lssc = 1,        // boat "LSSC", model lssc (0x897E)
    Lssc2 = 2,       // boat "LSSC", model mike2 (0x8DB0)
    Pbr = 4,         // boat "PBR", model mike2 (0x8DB0)
    Helicopter = 8,  // "UH-1"
};

// Fire-support / break-contact unit, MCI +0x72/+0x74. Names DS:25CC[v & ~1].
enum class SupportUnit : u16 {
    None = 0,
    Ov10 = 1,        // OV-10 pair (team type 3)
    BoatLssc = 2,    // boat pair (team type 1)
    BoatPbr = 4,     // boat pair (team type 1)
    Helicopter = 8,  // UH-1 pair (team type 2)
};

// Weapon class, WeaponDef::wclass (+0x04); selects hit-roll skill, sounds, flight.
enum class WeaponClass : s16 {
    None = 0,
    Rifle = 1,          // 56SKS, AK47
    Sniper = 2,         // SVD
    AssaultRifle = 3,   // M16A2, CAR15 (and M203 in rifle mode)
    GrenadeLauncher = 4,// M79, Mk18
    M203 = 5,           // M203 in grenade mode
    Smg = 6,            // M3A1, M76
    LightMg = 7,        // Stoner, K50
    Mg = 8,             // M60
    Shotgun = 9,        // M37
    Explosive = 10,     // DEMO, M26, T.M33, T.42, RDG33, T.M32
    Unused11 = 11,
    Smoke = 12,         // M18, SG1, Mk I illumination
    WhitePhosphorus = 13,// M15 WP, Mk3A2 stun
    TearGas = 14,       // M7
    RocketLauncher = 15,// M72 LAAW, RPG
    Rocket = 16,        // support rocket (Rkt)
    Minigun = 17,
    SilencedPistol = 18,// M39 Hushpuppy
    SwedishK = 19,      // M45
    Mortar = 20,        // T.31
};

// Weapon table index (DS:48FE, 34 entries). Names are read from the exe.
enum class WeaponId : s8 {
    None = -1,
    M3A1 = 0, M16A2 = 1, CAR15 = 2, M76 = 3, M39 = 4, M63Stoner = 5, M60 = 6, M37 = 7, M45 = 8,
    M79 = 9, M203 = 10, M72Laaw = 11, Demo = 12, M26Old = 13, M26 = 14, M15Wp = 15, M18Smoke = 16,
    M7TearGas = 17, Sks56 = 18, Ak47 = 19, Svd = 20, K50 = 21, TM33 = 22, T42 = 23, Rdg33 = 24,
    TM32 = 25, Sg1Smoke = 26, Rpg = 27, T31Mortar = 28, Minigun = 29, Rocket = 30, Mk3A2Stun = 31,
    Mk18 = 32, MkIIllum = 33,
};

// Fire-mode bits (WeaponDef::fire_modes +0x10, WeaponNode::fire_mode +0x0C).
namespace fire_mode {
constexpr u8 kSingle = 0x01;
constexpr u8 kSemi = 0x02;          // 3 rounds per trigger
constexpr u8 kFull = 0x04;          // 20 rounds per trigger
constexpr u8 kLauncher = 0x08;      // grenade launcher (M79/M203/Mk18)
constexpr u8 kThrow = 0x10;         // thrown ("grenade class")
constexpr u8 kBuckshot = 0x20;
constexpr u8 kIllumination = 0x40;  // Mk I (0x50 = throw + illumination)
}  // namespace fire_mode

// Item (tool) type, ItemNode::type; table far 52E3:0000 (12 entries).
enum class ItemType : s8 {
    None = -1,
    Radio = 0,        // PRC25
    MedicalKit = 1,
    Phk = 2,          // Prisoner Handling Kit
    Flare = 3,
    NightScope = 4,
    Documents = 5,
    Binoculars = 6,
    Cash = 7,
    Propaganda = 8,
    SurvivalKit = 9,
    Rice = 10,
    BoobyTrapKit = 11,
};

// Terrain/world object kind = model table entry +2 (WorldObject::kind).
enum class TerrainKind : u16 {
    Structure = 0,    // bunker, hooch, bldgston, well, tower, cache, church, pagoda, shelter (destructible)
    Vegetation = 3,   // reeds, tree4, bush, grasses, log, pineapple, bamboo2, flag
    Terrain = 4,      // paddy, mountain, beach, canal
    Clearing = 5,     // flat, path, bridgef, cemetery (no ground cover placed; NPCs walk over)
    Tree = 6,         // palm, jungle, banana2, tree2
    Water = 7,        // river, rivers
    Shallow = 8,      // stream(s), brook(s)
    DeepWater = 9,    // river2, riverl, sea, bay, riverh
    TripWire = 10,    // trip: booby-trap grenade
    PitTrap = 11,     // pit, stakes
    BoatPad = 14,     // dock
    HeloPad = 15,     // pad
    Prop = 16,        // oxcart, rock, boxes
    Brush = 17,       // pima, nipa
    Tunnel = 18,      // tunnel (the "special object" 0xC4B2)
    Dynamic = 19,     // units, ordnance, effects (never in .w files)
};

// Target / victim kind (TargetRec::kind, ShotRec::target_kind, victim list).
enum class TargetKind : s16 {
    None = -1,
    Unit = 0,
    Structure = 1,  // world object
    Craft = 2,      // victim lists only
    AimPoint = 3,   // no target: point 150 units ahead
};

// Animation state (Anim::state / return_state); argument of spr_set_anim.
enum class AnimState : s16 {
    Upright = 0, Crouch = 1, Prone = 2, Dead = 3,
    StandToCrouch = 4,   // uc
    CrouchToStand = 5,   // uc reversed
    DiveToProne = 6,     // up
    CrouchToProne = 7,   // cp
    ProneToCrouch = 8,   // cp reversed
    FallUpright = 9,     // uf
    Invisible = 10,
    ThrowReadyUp = 11,   // ua frame 0
    ThrowUp = 12,        // ua
    FallCrouch = 13,     // cf
    ThrowReadyCrouch = 14,  // ca frame 0
    ThrowCrouch = 15,    // ca
    ReloadCrouch = 16,   // cr, 0x300 ticks
    ShoulderFire = 17,   // cw, 0x300 ticks
    ThrowReadyProne = 18,// pa frame 0
    ThrowProne = 19,     // pa
};

// AI command type (Command::type).
enum class CommandType : u8 {
    None = 0,
    MoveMode = 1,   // arg = MoveMode
    Heading = 2,    // Command::heading
    Posture = 3,    // arg = Posture
    Fire = 4,       // arg bit0 primary, bit1 secondary
    Reload = 5,
    Look = 8,
    Surrender = 9,
};

// Group AI order type (TeamAi::order_type, MtmTeam::order_type).
enum class AiOrderType : u8 {
    None = 0,
    Patrol = 1,   // ping-pong along the MTM waypoints
    Hold = 2,     // no move orders until alerted
    Reserve = 3,  // delayed reinforcement: spawned hidden, deployed by timer or alarm
};

// Group AI behaviour (TeamAi::behaviour, MtmTeam::behaviour).
namespace ai_behaviour {
constexpr u8 kHunt = 0x10;   // moves to noises; set when alerted
constexpr u8 kGuard = 0x20;  // guards (no move orders when alerted)
constexpr u8 kWary = 0x30;   // ignores noise from passive NPC groups (exact meaning open)
}  // namespace ai_behaviour

// AI noise event type (NoiseEvent::type); levels far table 525C:type*2.
enum class NoiseType : s16 {
    Boat = 5, Aircraft = 6, Helicopter = 7, Walking = 8, Running = 9, Wading = 10,
    Cough = 0x0D, Sneeze = 0x0F, Explosion = 0x11, EnemyRadio = 0x12, Weapon = 0x13,
};

// Hand signals (msg_hand_signal, names DS:1CB0).
enum class HandSignal : s16 {
    Halt = 0, Danger = 1, Enemy = 2, Trap = 3, UnderFire = 4, Objective = 5, Search = 6,
    Secure = 7, Column = 8, InLine = 9, VeeWedge = 10, Diamond = 11, Cease = 12, AtWill = 13,
    Field = 14, Target = 15,
};

// ===========================================================================
// Bit flags
// ===========================================================================

// Obj3D::flags (+0x02).
namespace obj3d_flag {
constexpr u16 kEnabled = 0x0001;       // drawn / "visible" (game code sets and clears it)
constexpr u16 kStatic = 0x0002;        // record has no angles (0x12 bytes)
constexpr u16 kSkyHide = 0x0004;       // hidden while the whole frustum points upwards
constexpr u16 kInViewList = 0x0008;    // renderer bookkeeping
constexpr u16 kProjCache = 0x0010;     // may use the projection cache
constexpr u16 kGroup = 0x0020;         // container (unused by the game)
constexpr u16 kCullSkip = 0x0040;      // renderer bookkeeping
constexpr u16 kClipSkip = 0x0080;      // renderer bookkeeping
constexpr u16 kLosTreeB = 0x0100;      // in LOS quadtree B (terrain probes 1000:318E)
constexpr u16 kNoPrecise = 0x0200;     // never use precise projection
constexpr u16 kTargeted = 0x0400;      // current auto-target (byte +3 bit 2)
constexpr u16 kLinkedRecord = 0x0800;  // a near pointer follows the header (LOS filter)
constexpr u16 kLosTreeA = 0x1000;      // in LOS quadtree A (cover traces 1000:314C)
constexpr u16 kFreeHeading = 0x2000;   // LOS: arbitrary headings; vegetation: "kept" marker
constexpr u16 kStaticGrid = 0x4000;    // world_add_object argument only: static scenery
}  // namespace obj3d_flag

// Status::hit_mask (+0x0E) wound location bits (tables far 525F).
namespace hit_bit {
constexpr u16 kLightMask = 0x0133;     // 0x0001 0x0002 0x0010 0x0020 0x0100 -> light_wounds++
constexpr u16 kHeavyMask = 0x3ECC;     // 0x0004 0x0008 0x0040 0x0080 0x0200 0x0400 0x0800 0x1000 0x2000
constexpr u16 kLegLight = 0x0030;      // med_leg_level
constexpr u16 kLegHeavy = 0x00C0;
constexpr u16 kArmHeavy = 0x1800;      // med_arm_count (byte +0x0F bits 0x08/0x10)
constexpr u16 kPairA = 0x0005;         // med_head_count group {0x0001, 0x0004}
constexpr u16 kPairB = 0x000A;         // med_head_count group {0x0002, 0x0008}
constexpr u16 kKilled = 0x4000;        // dead (byte +0x0F bit 0x40)
}  // namespace hit_bit

// Mover::flags (+0x26).
namespace mover_flag {
constexpr u8 kBlocked = 0x01;     // bounced off an obstacle (cleared with the terrain bits)
constexpr u8 kSlowTerrain = 0x02; // +1 speed penalty; tested but never set
constexpr u8 kInWater = 0x04;     // terrain 7/8/9 (craft: on water near the destination)
constexpr u8 kDeepWater = 0x08;   // terrain 9 (wading sprite)
constexpr u8 kSearched = 0x10;    // body already searched
constexpr u8 kSecured = 0x20;     // prisoner / rescued / captured
constexpr u8 kAboard = 0x40;      // aboard the extraction craft
constexpr u8 kEscort = 0x80;      // helper/escort/carrier of Unit::buddy
}  // namespace mover_flag

// Mover::flags2 (+0x27).
namespace mover_flag2 {
constexpr u8 kFetchBuddy = 0x01;    // carrier walks back to a dead buddy
constexpr u8 kInBoat = 0x02;        // enemy placed on water: travels in a sampan (Unit::attached_fx)
constexpr u8 kObjectiveDone = 0x08; // on the target team's leader after Ambush/Rescue/Snatch
}  // namespace mover_flag2

// Brain::flags (+0x6C).
namespace brain_flag {
constexpr u8 kAlerted = 0x01;
constexpr u8 kEngaging = 0x04;
constexpr u8 kContact = 0x08;     // has contacts / has fired
constexpr u8 kSuppressed = 0x10;  // hit or near miss; ignores team commands until suppress_time runs out
constexpr u8 kFleeing = 0x20;
constexpr u8 kScript = 0x40;      // running a script sequence
constexpr u8 kActive = 0x80;      // deployed (reserves start inactive)
}  // namespace brain_flag

// Projectile::hit (+0x28) and Projectile::state (+0x29).
namespace prj_hit {
constexpr u8 kUnit = 0x01;
constexpr u8 kObstacle = 0x02;
constexpr u8 kLanded = 0x04;
constexpr u8 kPuffMask = 0xF0;    // impact puff kind 0x10/0x20/0x40/0x80
}  // namespace prj_hit
namespace prj_state {
constexpr u8 kResolved = 0x01;
constexpr u8 kExpired = 0x02;
constexpr u8 kDud = 0x10;         // fired from a jammed weapon
constexpr u8 kInUse = 0x80;
}  // namespace prj_state

// RosterEntry::flags (+0x12).
namespace roster_flag {
constexpr u8 kInTeam = 0x01;
constexpr u8 kWounded = 0x02;     // wounded last mission ("in the hospital")
constexpr u8 kReserve = 0x04;     // reserve recruit not yet introduced
constexpr u8 kKilled = 0x08;
}  // namespace roster_flag

// Award bits (RosterEntry::awards, CampaignHistory::awards); names DS:2542.
namespace award {
constexpr u16 kPurpleHeart = 0x0001;
constexpr u16 kNavyAchievement = 0x0004;
constexpr u16 kNavyCommendation = 0x0008;
constexpr u16 kBronzeStar = 0x0010;
constexpr u16 kSilverStar = 0x0020;
constexpr u16 kNavyCross = 0x0040;
constexpr u16 kMedalOfHonor = 0x0080;
constexpr u16 kNavalUnitCitation = 0x0100;         // << year (0..3)
constexpr u16 kPresidentialUnitCitation = 0x1000;  // << year (0..3)
}  // namespace award

// ===========================================================================
// 3D objects, models and the world
// ===========================================================================

// 3D object ("obj3d", body, instance) created by world_add_object (2255:0234)
// in the object arena g_obj_seg (DS:F450). 0x18 bytes (0x12 with kStatic).
struct Obj3D {
    const ModelDesc* model;  // +0x00 u16 near ptr (DS offset) of the model descriptor; game code swaps it for effects
    u16 flags;               // +0x02 obj3d_flag::*
    Obj3D* next;             // +0x04 u16 arena offset of the next object in its list
    Vec3 pos;                // +0x06 x, +0x0A altitude, +0x0E z (24.8; world_add_object stores game units << 8)
    Angle8 heading;          // +0x12 a0 (absent when kStatic)
    Angle8 pitch;            // +0x14 a1; model hooks of ripples/tunnel/boats/pit read it as an animation frame
    Angle8 roll;             // +0x16 a2 (bank)
};

// Model table entry, far 514B:0000, 97 entries of 6 bytes, zero terminated.
struct ModelTableEntry {
    const ModelDesc* desc;   // +0x00 u16 near ptr (DS offset) of the descriptor
    TerrainKind kind;        // +0x02 WorldObject::kind given to .w objects of this type
    u16 object_flags;        // +0x04 flags passed to world_add_object (obj3d_flag::*, e.g. 0x7101)
};

// Model descriptor (DGROUP, read from st.exe). Fields 0x00..0x2F from the
// renderer (2255), 0x30..0x49 from the LOS module (4FF8).
struct ModelDesc {
    u8 layer;                // +0x00 draw layer: < 0x80 flat layers (2 ground, 3 pad, 4 shadow, 9 sea, 0x0A river, 0x0C dock, 0x10 ripples), >= 0x80 depth sorted
    u8 unk_01;               // +0x01
    s16 radius;              // +0x02 bounding radius, model units
    s16 radius_x;            // +0x04 aspect-corrected horizontal radius (computed at load)
    s16 radius_y;            // +0x06 aspect-corrected vertical radius (computed at load)
    s32 radius_world;        // +0x08 bounding radius << (8 + scale_shift)
    s8 scale_shift;          // +0x0C 1 model unit = 2^scale_shift game units
    u8 unk_0d;               // +0x0D high byte of the +0x0C word (0x1F on houses, towers; not read)
    u16 lod_distance[3];     // +0x0E LOD thresholds (units of 65536 world units; 0 unused)
    u16 lod_model[3];        // +0x14 near ptrs of the LOD models (game models use only [0])
    s16 size_class;          // +0x1A world-box cull class 10..19 (>= 20 never culled)
    void* lod_callback;      // +0x1C far code ptr int f(obj, lod); segment 0 = none
    u16 unk_20;              // +0x20
    const char* pnt_name;    // +0x22 near ptr: base name of the .pnt file (also the HUD target name)
    void* swap_copy;         // +0x24 far copy for the swap buffer (always 0 in the game)
    u16 swap_reloc;          // +0x28
    u16 swap_size;           // +0x2A
    u16 default_height;      // +0x2C initial altitude given by world_add_object (world units)
    u16 game_flags;          // +0x2E not used by the renderer
    s32 box_min_x;           // +0x30 collision box (world units, object space)
    s32 box_max_x;           // +0x34
    s32 box_min_y;           // +0x38
    s32 box_max_y;           // +0x3C
    s32 box_min_z;           // +0x40
    s32 box_max_z;           // +0x44
    const void* heightmap;   // +0x48 near ptr to an optional heightmap descriptor (mountain)
};

// Structure / terrain / world object: 14-byte record per .w entry plus 126
// spare vegetation objects (g_spare_objects DS:31BA); far pointer table
// g_world_objects (DS:26F4), count g_world_object_count (DS:ECB6). Also the "structure" of objectives and demolition.
struct WorldObject {
    const ModelDesc* model;  // +0x00 u16 near ptr of the model ("shape"; replaced by the destroyed shape)
    Obj3D* body;             // +0x02 far 3D instance
    TerrainKind kind;        // +0x06 from the model table
    u16 flags;               // +0x08 .w flags; bit 0 = objective completed (Patrol/Demolition/Observe/Recover)
    s16 hit_points;          // +0x0A < 1 = destroyed (kind 0x10: 0x32, kind 3: 0x19)
    u8 cover;                // +0x0C line-of-sight cover 0..100 added by wld_los_cover (100 = opaque)
    u8 height;               // +0x0D >= 0x7F solid (blocks movement)
};

// ===========================================================================
// Units
// ===========================================================================

// Unit (soldier, craft, NPC): 0x34-byte entity from ent_alloc (4511:019E),
// optional components by the allocation flags.
struct Unit {
    const ModelDesc* model;  // +0x00 u16 near ptr set by unit_set_class_word: 0x862A human, boat 0x897E (LSSC)/0x8DB0 (Mike), helo 0x726A (Cobra)/0xC5B2 (UH-1), 0x992C OV-10
    Obj3D* body;             // +0x02 far world object: position, heading (Angle8)
    Status* status;          // +0x06 far, component bit 0x01 (0x1C bytes)
    Mover* mover;            // +0x0A far, component bit 0x02 (0x48 bytes)
    Anim* anim;              // +0x0E far, component bit 0x04 (0x26 bytes)
    Brain* brain;            // +0x12 far, component bit 0x08 (0xD0 bytes); NULL for the Point Man
    Loadout* loadout;        // +0x16 far weapon list header (12 bytes)
    ItemNode* items;         // +0x1A far tool/item list head
    Team* team;              // +0x1E far owning team
    u16 unk_22;              // +0x22 never written (zero)
    Obj3D* attached_fx;      // +0x24 far effect object riding with the unit: shadow/wake (fx_unit_ground_fx) or the team's sampan (fx_team_marker)
    RosterEntry* roster;     // +0x28 far campaign roster entry (SEALs; NULL for NPCs and craft)
    SeRecord* se;            // +0x2C far personnel record (name +0x0C, camouflage +0x62)
    Unit* buddy;             // +0x30 far linked unit (casualty <-> helper, prisoner <-> escort), linked both ways
};

// Unit status ("STATUS", personnel/body block): 0x1C bytes. Bytes 0..0x1B
// are a copy of SE+0x74..+0x8F (skills overwritten by the roster skills).
struct Status {
    u8 skill[8];             // +0x00..+0x07 Skill: rifle, pistol, mortar, shoulder, automatic, throw, observe, radio (0..90)
    u8 size;                 // +0x08 body size: load capacity, sprite scale, AI visibility, and the +30/+10 hit-roll bonus
    u8 strength;             // +0x09 load capacity = size/2 + strength
    u8 agility;              // +0x0A reduces noise
    u8 intelligence;         // +0x0B
    u8 experience;           // +0x0C "Time In Service": +2 per mission flown (cap 90), AI sight range, noise
    u8 unk_0d;               // +0x0D
    u16 hit_mask;            // +0x0E hit_bit::* wound locations; 0x4000 = dead
    u16 unk_10;              // +0x10 cleared at spawn
    u8 light_wounds;         // +0x12
    u8 heavy_wounds;         // +0x13 non-zero = seriously wounded
    u8 bleeding;             // +0x14 1 = bleeding / dying
    u8 unk_15;               // +0x15
    s16 bleed_time;          // +0x16 ticks left before bleeding to death
    u16 load;                // +0x18 carried load, 1/10 lb
    UnitClass unit_class;    // +0x1A stored by unit_init_body; >= 5 ignores wounds (craft, also civilians/friendlies)
};

// Movement component ("MOVER", motion block): 0x48 bytes. Soldiers use the
// posture fields; craft re-use +0x25 as altitude mode. Speeds are map units
// per 256 ticks; headings whole degrees.
struct Mover {
    s16 speed;               // +0x00 current speed (negative = backwards)
    s16 target_speed;        // +0x02
    s16 base_speed;          // +0x04 soldiers 0x18, boat 0x40, helo 0x80, OV-10 0x2D0
    s16 unk_06;              // +0x06 init -12 soldiers, -0x20 boat/helo, 0x18 OV-10 (no reader found; minimum speed?)
    s16 accel;               // +0x08 approach rate toward target_speed
    s16 decel;               // +0x0A rate used when the target speed is 0
    s16 heading;             // +0x0C deg; body heading = heading * 8
    s16 desired_heading;     // +0x0E deg
    s16 unk_10;              // +0x10 always 0x168 (360)
    s16 unk_12;              // +0x12 always 0
    s16 turn_rate;           // +0x14 soldiers 0x70 (>> posture), craft 0x30/0x20
    s16 unk_16;              // +0x16 not initialised
    s16 height;              // +0x18 current height (Point Man: eye height; negative = sunk into water)
    s16 target_height;       // +0x1A soldiers: eye height {15,9,3} or wading height; craft: target altitude
    s16 height_high;         // +0x1C soldiers 15; craft high/cruise altitude
    s16 height_low;          // +0x1E soldiers 3; craft low altitude
    s16 climb_rate;          // +0x20
    s16 descent_rate;        // +0x22
    MoveMode move_mode;      // +0x24
    Posture posture;         // +0x25 soldiers; craft: AltitudeMode (cast)
    u8 flags;                // +0x26 mover_flag::*
    u8 flags2;               // +0x27 mover_flag2::*
    Vec3 destination;        // +0x28
    s16 impact_bearing;      // +0x34 deg: bearing of the last obstacle / incoming hit (wounded units turn to it)
    s16 aim_heading;         // +0x36 deg: facing while firing (sprite, head rotation)
    s16 fatigue;             // +0x38 0..8, speed penalty
    s16 winded;              // +0x3A 0..8, breath sounds; set to 4 on the Point Man at insertion
    Ticks fired_until;       // +0x3C g_time + 0x100 after firing: speed penalty, sprite faces aim_heading
    Ticks throw_until;       // +0x40 g_time + 0x200 after a throw
    Ticks posture_until;     // +0x44 soldiers: g_time + 0x200 after a posture change; craft: radio busy until
};

// Animation component ("ANIM", segment 348e): 0x26 bytes.
struct Anim {
    u8 zero_00;              // +0x00 always 0
    u8 kind;                 // +0x01 colour-remap scheme (>= 3 uses remap2..5)
    u8 unk_02[4];            // +0x02
    s16 posture;             // +0x06 Posture incl. 3 = dead (set when spr_set_anim(s <= 3))
    s16 posture_copy;        // +0x08 written together with +0x06
    s16 move_mode;           // +0x0A mirror of the move mode (0 on stop/casualty, AI writes it); not read by 348e
    Ticks created;           // +0x0C g_time at spr_init_unit_anim
    SpriteFrame* sets;       // +0x10 far ptr to the soldier sprite sets (53BA:015E)
    u8 unk_14[4];            // +0x14
    AnimState state;         // +0x18
    AnimState return_state;  // +0x1A state after the timed animation
    Ticks start;             // +0x1C clock value at the start of the timed animation
    s16 duration;            // +0x20
    Ticks clock;             // +0x22 advanced by g_frame_ticks for foot teams; starts random(256)
};

// ===========================================================================
// AI
// ===========================================================================

// AI command: 0x14 bytes, rings of 5 in Brain and TeamAi, and in script steps.
struct Command {
    u8 arg;                  // +0x00 MoveMode / Posture / fire weapon mask
    CommandType type;        // +0x01 (class << 8 | arg)
    u8 flag;                 // +0x02
    u8 unk_03;               // +0x03
    Vec3 pos;                // +0x04 fire/look position
    u8 ai_issued;            // +0x10 1 when issued by the AI (turn commands)
    u8 unk_11;               // +0x11
    s16 heading;             // +0x12 deg for type Heading
};

// Target record, 0x1E bytes. Same layout for the player's reticle target
// (far 5149:0000), the search log (far 53BA:0000, 9 records) and the three
// AI contact slots of a Brain (+0x74).
struct TargetRec {
    u8 in_use;               // +0x00 contacts: slot used
    u8 unk_01;               // +0x01
    Vec3 pos;                // +0x02 aim point / last seen position
    union {
        Unit* unit;              // kind Unit
        WorldObject* structure;  // kind Structure
    } target;                // +0x0E far
    TargetKind kind;         // +0x12
    const Vec3* target_pos;  // +0x14 far ptr to the target's live position (contacts)
    s16 range;               // +0x18 distance in map units
    u8 score;                // +0x1A contact priority (ai_target_priority)
    u8 sighted;              // +0x1B contact from a real sighting (0 = borrowed/synthetic)
    s16 cover;               // +0x1C LOS cover sum toward the target (< 100 visible); passed to shot_fire
};

// Unit AI ("brain", "mind"): 0xD0 bytes.
struct Brain {
    Command commands[5];     // +0x00 own command ring
    u8 cmd_index;            // +0x64 next write slot; newest = (cmd_index + 4) % 5
    u8 cmd_pending;          // +0x65
    u8 surrendered;          // +0x66 hands-up pose (also 1 for friendly NPCs)
    u8 unk_67;               // +0x67
    Team* team;              // +0x68 far back pointer to the unit's team
    u8 flags;                // +0x6C brain_flag::*
    u8 unk_6d;               // +0x6D
    s16 script_delay;        // +0x6E ticks until the next script step
    ScriptStep* script;      // +0x70 far current script step
    TargetRec contacts[3];   // +0x74 contact slots
    s16 suppress_time;       // +0xCE ticks of suppression left (flag kSuppressed)
};

// Group AI record: 0x8E bytes (ent2_alloc bit 3), Team::ai.
struct TeamAi {
    Vec3 destination;        // +0x00
    Ticks deploy_timer;      // +0x0C reserve groups: ticks until deployment
    Ticks think_timer;       // +0x10 reaction delay (only the low word is initialised)
    ScriptStep* script;      // +0x14 far (group scripts: unreferenced code only)
    Command commands[5];     // +0x18 team command ring
    u8 cmd_index;            // +0x7C
    u8 cmd_pending;          // +0x7D
    u8 script_active;        // +0x7E
    u8 blocked;              // +0x7F
    u8 unk_80;               // +0x80 cleared
    u8 look_counter;         // +0x81 every 5th decision issues a look command
    AiOrderType order_type;  // +0x82 from MtmTeam::order_type
    u8 behaviour;            // +0x83 ai_behaviour::*
    WaypointNode* cur_waypoint;  // +0x84 far
    WaypointNode* waypoints;     // +0x88 far list head
    s8 patrol_dir;           // +0x8C 1 forward, 0 backward (ping-pong)
    u8 unk_8d;               // +0x8D
};

// Waypoint list node, 0x14 bytes (4511 ent_add_waypoint).
struct WaypointNode {
    Vec3 pos;                // +0x00
    WaypointNode* prev;      // +0x0C far
    WaypointNode* next;      // +0x10 far
};

// Script step, 0x1A bytes; three built-in sequences at far 53BA:268C.
struct ScriptStep {
    Command cmd;             // +0x00
    s16 delay;               // +0x14 ticks before this step is issued
    ScriptStep* next;        // +0x16 far
};

// ===========================================================================
// Teams
// ===========================================================================

// Team ("group"): 0x34 bytes (ent2_alloc). g_groups (DS:12E2) is a NULL
// terminated array of far pointers, g_team_count (DS:EC87) entries; team 0
// member 0 = the Point Man (the player).
struct Team {
    Unit* members[8];        // +0x00 far, NULL terminated; [0] = leader
    Formation formation;     // +0x20
    FireOrder fire_order;    // +0x22 (craft: always AtWill)
    s16 order;               // +0x24 TeamOrder (foot teams) or CraftOrder (TeamType 1..3)
    TeamType type;           // +0x26
    TeamAi* ai;              // +0x28 far
    s32 map_height;          // +0x2C map zoom (camera height 24.8): 256000 SEAL, 512000 others; -1 = derive from distance
    s16 view_distance;       // +0x30 orbit camera distance (0x60..900, initial 0x60)
    s16 view_heading;        // +0x32 orbit camera angle; -1 = recompute
};

// ===========================================================================
// Weapons and items
// ===========================================================================

// Weapon table entry, DS:48FE, 34 entries of 0x22 bytes (read from st.exe).
struct WeaponDef {
    const char* short_name;  // +0x00 near ptr ("M16A2")
    const char* long_name;   // +0x02 near ptr ("M16")
    WeaponClass wclass;      // +0x04
    u16 availability;        // +0x06 bit y (0..3) = SEAL-selectable in 1966+y; 0x10/0x20 = enemy weapons
    s16 range_short;         // +0x08 range bands (map units)
    s16 range_medium;        // +0x0A
    s16 range_max;           // +0x0C
    s16 magazine;            // +0x0E rounds per reload
    u16 fire_modes;          // +0x10 fire_mode::* allowed
    s16 blast_radius;        // +0x12 0 = bullet
    s16 structure_damage;    // +0x14 damage to world objects / craft
    s16 noise;               // +0x16 noise level / 4 (noise_from_weapon)
    s16 weight;              // +0x18 1/10 lb
    s16 reload_weight;       // +0x1A 1/10 lb per reload (0 = use weight)
    s16 jam;                 // +0x1C jam if random(100) <= max(1, jam/2)
    s16 reload_ticks;        // +0x1E reload time with "Reload Time: Timed"
    s16 default_reloads;     // +0x20 default / maximum reloads in the loadout
};

// Item/tool table entry, far 52E3:0000, 12 entries of 6 bytes.
struct ItemDef {
    const char* short_name;  // +0x00 near ptr ("PRC25")
    const char* long_name;   // +0x02 near ptr ("Radio")
    s16 weight;              // +0x04 1/10 lb
};

// Weapon slot node, 0x12 bytes (ent_weapon_init).
struct WeaponNode {
    WeaponId type;           // +0x00
    u8 unk_01;               // +0x01
    s16 rounds;              // +0x02 rounds in the magazine (init magazine)
    s16 reloads;             // +0x04 spare magazines (8 when -1 was passed)
    s16 m203_count;          // +0x06 M203 grenades fired since reload (0..3)
    u8 jammed;               // +0x08 1 = jammed (next shot is a dud)
    u8 unk_09;               // +0x09
    s16 unk_0a;              // +0x0A init 3
    u8 fire_mode;            // +0x0C selected fire_mode bit
    u8 unk_0d;               // +0x0D
    WeaponNode* next;        // +0x0E far
};

// Tool/item node, 8 bytes.
struct ItemNode {
    ItemType type;           // +0x00
    u8 unk_01;               // +0x01
    s16 quantity;            // +0x02 (8 when -1 was passed)
    ItemNode* next;          // +0x04 far
};

// Weapon list header (unit+0x16), 12 bytes.
struct Loadout {
    WeaponNode* list;        // +0x00 far first node
    WeaponNode* primary;     // +0x04 far current weapon
    WeaponNode* secondary;   // +0x08 far current grenade / second weapon
};

// ===========================================================================
// Ordnance and shots
// ===========================================================================

// Projectile motion record ("flight record"): the 0x48-byte block at
// Projectile+6 has the Mover layout; ordnance uses these members.
struct FlightRec {
    s16 speed;               // +0x00 horizontal speed (bullets 0x168, M203 0x1E0, thrown 0x5A, smoke 0x60, gas 0x48, LAAW/RPG 0x258, rocket 0x21C, mortar 0x3C)
    s16 target_speed;        // +0x02 same value
    s16 max_speed;           // +0x04 0x3E7 for bullets
    s16 unk_06;              // +0x06
    s16 accel;               // +0x08 0x20
    s16 unk_0a;              // +0x0A
    s16 heading;             // +0x0C
    s16 unk_0e;              // +0x0E
    s16 unk_10;              // +0x10 0x168
    s16 unk_12;              // +0x12
    s16 turn_rate;           // +0x14 0x20
    s16 unk_16;              // +0x16
    s16 vertical_speed;      // +0x18 launch velocity from evt_calc_launch_velocity (down positive)
    s16 unk_1a;              // +0x1A 9
    s16 unk_1c;              // +0x1C class constant (-0x10 bullets .. 0xF0 thrown; looks like a launch pitch)
    s16 launch_height;       // +0x1E 3 thrown/smoke/gas, 6 rocket
    s16 bounces;             // +0x20 1 = bounces on the ground (thrown items)
    s16 gravity;             // +0x22 1 = affected by gravity (0x3C * dt / 256)
    u8 unk_24[0x24];         // +0x24..+0x47 rest of the mover layout (unused)
};

// Projectile / ordnance slot: 0x50 bytes, 32 in g_fx_pool (DS:2670, fx_alloc(2)).
struct Projectile {
    const ModelDesc* base_model;  // +0x00 u16 model restored after effects (0x832E = 40 mm grenade)
    Obj3D* body;             // +0x02 far
    FlightRec* flight;       // +0x06 far
    WeaponClass wclass;      // +0x0A ordnance class (weapon table +4; M203 rifle mode -> 3)
    s16 weapon;              // +0x0C WeaponId (0x0A M203, 0x0C DEMO)
    u16 fire_modes;          // +0x0E weapon table +0x10 (satchel: 8)
    Ticks launch_time;       // +0x10
    Ticks muzzle_phase;      // +0x14 duration of the muzzle/hand phase: 0x40, buckshot 0x20, classes 0x0F/0x10/0x14 0x180 (0 for direct shots)
    Ticks timer_base;        // +0x18 = launch time
    Ticks impact_time;       // +0x1C
    Ticks lifetime;          // +0x20 0x600, smoke/gas 0x3C00, flares 0xF000+, direct 0x700, demo 0x4000/0x5000
    Ticks detonate_time;     // +0x24 launch + 0x400 (demo + 0x3C00)
    u8 hit;                  // +0x28 prj_hit::*
    u8 state;                // +0x29 prj_state::*
    Unit* target;            // +0x2A far intended target
    union {
        Unit* unit;
        WorldObject* structure;
    } last_hit;              // +0x2E far last collision (unit or feature)
    Unit* owner;             // +0x32 far shooter
    WeaponNode* weapon_node; // +0x36 far weapon slot fired
    u8 unk_3a[6];            // +0x3A
    Obj3D* fx_muzzle;        // +0x40 far muzzle flash
    Obj3D* fx_explosion;     // +0x44 far explosion
    Obj3D* fx_cloud;         // +0x48 far smoke/gas cloud
    Obj3D* fx_impact;        // +0x4C far impact puff
};

// Shot (combat) record: 0x3C bytes, 32 at far 53BA:28BA.
struct ShotRec {
    u8 state;                // +0x00 0 = in flight, 1 = resolved/free
    u8 unk_01;               // +0x01
    Unit* shooter;           // +0x02 far
    Vec3 shooter_pos;        // +0x06
    s16 shooter_move;        // +0x12 MoveMode
    s16 shooter_posture;     // +0x14 Posture
    TargetKind target_kind;  // +0x16
    Unit* target;            // +0x18 far (NULL for aim points)
    s16 target_move;         // +0x1C (0 unless target_kind == Unit)
    s16 target_posture;      // +0x1E
    u8 unk_20;               // +0x20 0
    u8 unk_21;               // +0x21
    s16 weapon;              // +0x22 WeaponId
    s32 range;               // +0x24 distance to the target
    u8 fire_mode;            // +0x28 fire_mode bit used
    u8 rounds;               // +0x29 rounds in this burst
    s16 cover;               // +0x2A LOS cover toward the target (0 for the player): >= 75 -30, >= 50 -10, >= 25 -5
    Vec3 target_pos;         // +0x2C
    Projectile* projectile;  // +0x38 far trajectory/impact object from prj_fire
};

// Victim list entry used while resolving a shot (stack array of 16).
struct ShotVictim {
    union {
        Unit* unit;
        WorldObject* structure;
    } who;                   // +0x00 far
    s16 band;                // +0x04 range band 0..2
    u8 kind;                 // +0x06 TargetKind (3 = empty entry)
    u8 unk_07;               // +0x07
};

// Wound table entry, far 525F: 5 tables (bullet band 0..2, blast band 0..1)
// of 17 entries (last all zero), 0x44 bytes each.
struct WoundTableEntry {
    u8 lo;                   // +0x00 first roll
    u8 hi;                   // +0x01 last roll
    u16 bits;                // +0x02 hit bits; 0xFFFF miss, 0 no effect, 0x4000 killed
};

// AI noise event: 0x18 bytes, 20 at far 53BA:26DA.
struct NoiseEvent {
    NoiseType type;          // +0x00
    s16 level;               // +0x02
    s16 heard;               // +0x04 set on the first noise_update, freed on the second
    Team* source;            // +0x06 far source team
    Vec3 pos;                // +0x0A
    s16 free;                // +0x16 1 = slot free
};

// ===========================================================================
// Presentation
// ===========================================================================

// Camera: 0x1C bytes; g_cam_main (DS:D84E), g_cam_map (DS:D86E),
// g_cam_small (DS:D88A); g_cur_camera (DS:D8AE) points to one of them.
struct Camera {
    Vec3 pos;                // +0x00 (+0x04 = altitude; g_map_height DS:D872 is the map camera's)
    Angle8 yaw;              // +0x0C
    Angle8 pitch;            // +0x0E (view_init_camera: pitch << 3; map -90 deg)
    Angle8 roll;             // +0x10
    s16 rect_x;              // +0x12 screen viewport
    s16 rect_y;              // +0x14
    s16 rect_w;              // +0x16
    s16 rect_h;              // +0x18
    s16 zoom;                // +0x1A projection zoom shift, always 8
};

// Message queue entry: 0x52 bytes, ring of 8 in g_msg_queue (DS:3716).
struct MsgEntry {
    s16 style;               // +0x00 -1 free, 0/1 dialog lines, 2 HUD line, 3 hand-signal icon line, 6 special
    s16 sticky;              // +0x02 the last sticky message cannot be removed
    s16 duration;            // +0x04 ticks; 0 = shown with the previous message
    Ticks expiry;            // +0x06 -1 = not started
    s16 kind;                // +0x0A 0..2 dialog font DS:EEF4[kind], 3 HUD overlay, 4 medal picture (frame style % 8)
    const char* text_ptr;    // +0x0C far original text
    char text[64];           // +0x10 copy ("Dlg Str Overflow" when >= 63 chars)
    s16 icon;                // +0x50 hand-signal number (style 3)
};

// Briefing camera waypoint: 16 bytes, ring of 8 at far 5170:0000.
struct BrfCamWaypoint {
    s16 flag;                // +0x00 -1 = empty
    s16 unk_02;              // +0x02 always 1
    s16 duration;            // +0x04 ticks
    Ticks end_time;          // +0x06 -1 = not started
    s16 height;              // +0x0A camera height (map units)
    const Vec3* target;      // +0x0C far target position
};

// Button record: 16 bytes. Map control panel DS:0BF4 (41 buttons) and the
// front-end lists (365e), terminated by key == 0.
struct ButtonRecord {  // runtime form with label: game/ui.h Button
    s16 x;                   // +0x00
    s16 y;                   // +0x02
    s16 w;                   // +0x04
    s16 h;                   // +0x06 hit box uses h - 2
    u16 key;                 // +0x08 key code injected (ASCII or scan << 8)
    s8 underline;            // +0x0A index of the underlined hot-key letter, -1 none
    u8 flags;                // +0x0B 0x01 clickable, 0x02 disabled (hatched), 0x04 label not drawn, 0x10 highlighted, 0x20 extra frame, 0x40 invisible hot spot
    const char* label;       // +0x0C far label (map buttons, bound by map_init); 0 in front-end lists (parallel label arrays)
};

// Sound channel: 0x2A bytes, 6 at far 53BA:258C.
struct SoundChannel {
    s16 sfx_id;              // +0x00 effect 1..53
    u16 flags;               // +0x02 bit0 active, bit1 from descriptor, bit3 unpositioned, bit4 on the digital voice
    s16 fm_param;            // +0x04
    Ticks start;             // +0x06
    Ticks lifetime;          // +0x0A
    Vec3 pos;                // +0x0E
    s16 unk_1a;              // +0x1A
    s16 fm_sequence;         // +0x1C -1 none
    void* fm_state;          // +0x1E far FM state table
    u8 fm_controllers[2];    // +0x22 {fm_param, 0x7F}
    Unit* attached;          // +0x24 far unit whose body position is followed
    s16 digi_chunk;          // +0x28
};

// Sound-effect descriptor: 0x16 bytes at far 520D:000C + id*0x16.
struct SfxDesc {
    u16 distance;            // +0x00 audible distance (map units)
    u16 flags;               // +0x02 bit1 positional
    u8 has_fm;               // +0x04
    u8 has_digital;          // +0x05 sfxNN.voc
    void* sample;            // +0x06 far (run time)
    u16 fm_sequence;         // +0x0A id + 10 (run time)
    void* fm_xmi;            // +0x0C far (run time)
    s16 ems_handle;          // +0x10 -1 (run time)
    u16 priority;            // +0x12
    u16 sample_size;         // +0x14 (run time)
};

// Sprite set frame: 0x50 bytes (sets in far 53BA).
struct SpriteFrame {
    void* image[8];          // +0x00 far RLE image per rotation (NULL when in EMS)
    s16 ems[8];              // +0x20 EMS handle per rotation, -1 none
    void* rlx[8];            // +0x30 far 12-byte RlxRecord per rotation
};

// .RLX record (18-byte file, first 12 bytes used).
struct RlxRecord {
    u16 zero_00;             // +0x00
    u16 anchor_x;            // +0x02 ground point
    u16 anchor_y;            // +0x04
    u8 head_frame;           // +0x06 headgear frame 0..4
    u8 layer;                // +0x07 > 0x80: headgear drawn behind the body
    s16 head_x;              // +0x08
    s16 head_y;              // +0x0A
};

// Medal of Honor table entry, far 52BB:001A, 9 entries.
struct MohRecord {
    u16 year;                // +0x00
    u16 mission;             // +0x02 index in the year
    u16 min_score;           // +0x04
};

// ===========================================================================
// Files and campaign data
// ===========================================================================

// Mission objective (0x62 bytes, three in the MCI; the third is truncated).
struct MciObjective {
    ObjectiveKind kind;      // +0x00
    Vec3 pos;                // +0x02 objective position (camera target, craft placement)
    s16 target_team;         // +0x0E MTM index (team g_first_mtm_group + n), -1 none
    s16 target_structure;    // +0x10 index into g_world_objects, -1 none
    char description[40];    // +0x12 target description
    char route[40];          // +0x3A "You will move <route>" (absent for objective 3)
};

// Mission header cYmNN.mci (msns.lib), exactly 0x176 bytes; run-time copy g_mci
// (DS:ED32), pointer g_mission (DS:EEB8).
struct MciHeader {
    u16 start_hour;          // +0x00
    u16 start_minute;        // +0x02
    u16 world;               // +0x04 area/world index 0..27 (names DS:1CD0)
    InsertionMethod insertion_method;   // +0x06
    InsertionMethod extraction_method;  // +0x08
    Vec3 insertion;          // +0x0A
    Vec3 extraction;         // +0x16
    char insertion_desc[40]; // +0x22 (not used by the code)
    char extraction_desc[40];// +0x4A (not used by the code)
    SupportUnit fire_support;   // +0x72 0 none, 1 OV-10 pair, 2/4 boat pair, 8 helicopter pair
    SupportUnit break_contact;  // +0x74 same codes (spawned only when fewer than 3 craft)
    MciObjective objective[3];  // +0x76, +0xD8, +0x13A (third: only +0x00..+0x39 exist)
    u16 mtm_count;           // +0x174 MTM records to spawn
};
constexpr std::size_t kMciSize = 0x176;
constexpr std::size_t kMciObjectiveSize = 0x62;

// Mission team record cYmNN.mtm (msns.lib), n x 0x66 bytes, n <= 15;
// g_mission_groups (DS:39CE).
struct MtmTeam {
    MtmKind kind;            // +0x00
    Vec3 start;              // +0x02
    u16 member_count;        // +0x0E 1..5; reduced in place by the Team Size option
    u16 members[8];          // +0x10 vcNN numbers, 1-based, clamped to 1..21
    u16 formation;           // +0x20 Formation of the group
    AiOrderType order_type;  // +0x22 1 patrol, 2 hold, 3 reserve (spawned hidden, crouched/prone)
    u8 behaviour;            // +0x23 ai_behaviour::* 0x10/0x20/0x30
    u16 deploy_delay;        // +0x24 reserve delay code 1..9
    u16 unk_26;              // +0x26 0/1, no reader found
    u16 heading;             // +0x28 deg, heading of member 0
    Vec3 waypoints[5];       // +0x2A patrol waypoints (y ignored, (0,0) unused)
};
constexpr std::size_t kMtmTeamSize = 0x66;
constexpr std::size_t kMtmMaxTeams = 15;

// Personnel record *.SE (se.lib), 0x90 bytes: sealNN, vcNN, civ01, frndNN.
struct SeRecord {
    char first_name[12];     // +0x00
    char last_name[20];      // +0x0C (speaker name)
    char nickname[26];       // +0x20 NPCs: descriptive tag ("Avg-Rifleman-66")
    char birthplace[32];     // +0x3A "Lexington, KY\n"
    u8 height_ft;            // +0x5A
    u8 height_in;            // +0x5B
    u8 weight_lb;            // +0x5C
    u8 buds_class;           // +0x5D BUD/S class name index (0xFF some SEALs); bit 0 -> "SEAL Team 2"
    u8 rating;               // +0x5E Navy rating 0..12 (DS:1BAA / DS:1BC4)
    u8 age;                  // +0x5F at the campaign start (+ campaign year when shown)
    u8 unk_60;               // +0x60 never read (SEALs 0..12, NPCs 8)
    u8 rank;                 // +0x61 0..11 (DS:2506); updated by promotions
    u8 camouflage;           // +0x62 DS:2536 names; SEAL headgear v%6 (0/1 -> bush hat); NPC cache: index + 1
    s8 weapons[8];           // +0x63 WeaponId list, -1 terminated; SEALs: [0] best, [1] second, [2]/[3] slots C/D
    s8 items[8];             // +0x6B ItemType list, -1 terminated
    u8 unk_73;               // +0x73 0
    u8 skill[8];             // +0x74 Skill 0..7 (Status+0x00)
    u8 size;                 // +0x7C Status+0x08
    u8 strength;             // +0x7D Status+0x09
    u8 agility;              // +0x7E Status+0x0A
    u8 intelligence;         // +0x7F Status+0x0B
    u8 experience;           // +0x80 Time In Service, Status+0x0C
    u8 unk_81[11];           // +0x81 zero in the files; image of Status+0x0D..+0x17
    u16 load;                // +0x8C carried load 1/10 lb (marching order), Status+0x18
    u16 unk_8e;              // +0x8E Status+0x1A
};
constexpr std::size_t kSeSize = 0x90;

// Campaign history entry (12 bytes), point man's missions.
struct CampaignHistory {
    u16 mission;             // +0x00 year*20 + index
    s32 score;               // +0x02
    u16 awards;              // +0x06 award::* earned this mission (last write wins)
    u16 casualties;          // +0x08 point man hit bits (byte +9 bit 0x40 = killed)
    u8 rank;                 // +0x0A new rank if promoted, 0 none
    u8 won;                  // +0x0B
};

// Campaign header g_campaign_state (DS:3B0E; pointer g_campaign DS:EEC2),
// 0x14C bytes; first part of cN.cmp.
struct CampaignHeader {
    s8 slot;                 // +0x00 save slot 0..8 (8 = autosave), -1 none/practice; rewritten on load/save
    u8 year;                 // +0x01 0..3 (1966..1969)
    u8 mission;              // +0x02 index in the year 0..19; 0xFF = campaign over
    u8 last_won;             // +0x03 result of the last mission (init 1)
    s16 point_man;           // +0x04 SE number 0..47
    s32 total_score;         // +0x06
    s32 best_score;          // +0x0A
    char nickname[28];       // +0x0E typed on the recruit screen (<= 14 chars; empty = SE nickname)
    CampaignHistory history[24];  // +0x2A indexed by the point man's mission count
    s8 pm_weapon_a;          // +0x14A point man's slot A weapon
    s8 pm_weapon_b;          // +0x14B point man's slot B weapon
};
constexpr std::size_t kCampaignHeaderSize = 0x14C;

// Roster entry, 0x18 bytes; g_roster (DS:3A6A) holds up to 40 far pointers
// (NULL terminated); rest of cN.cmp.
struct RosterEntry {
    u8 se_id;                // +0x00 0..47 -> sealNN.se (NN = id + 1)
    u8 missions;             // +0x01 flown (history index of the point man)
    u8 wins;                 // +0x02
    u8 rank;                 // +0x03 mirrors SE+0x61
    u16 awards;              // +0x04 accumulated award::*
    u16 unk_06;              // +0x06
    u16 casualties;          // +0x08 accumulated hit bits (0x4000 killed)
    u8 skill[8];             // +0x0A weapon skills (SE+0x74..0x7B), trained after missions
    u8 flags;                // +0x12 roster_flag::*
    u8 unk_13;               // +0x13
    SeRecord* se;            // +0x14 far loaded sealNN.se (run time)
    u32 file_se_ptr;         // (not in the record) raw +0x14 value read from the file, kept for byte-exact re-saves
};
constexpr std::size_t kRosterEntrySize = 0x18;
constexpr std::size_t kRosterMax = 40;

// Campaign flow table entry: far 5275:0000/0118/0230/0348, 4 years x 20 x 14 bytes.
struct FlowEntry {
    u16 stage;               // +0x00 0..5 in the year
    u16 month;               // +0x02 1..12 (DS:1B90)
    u16 day;                 // +0x04 (not used; screens show index + 1)
    u16 time_hhmm;           // +0x06 (not used)
    u16 next_if_lost;        // +0x08
    u16 next_if_won;         // +0x0A 0 = end of the year
    u16 flags;               // +0x0C bit0 historic report hYmNN.s exists, bit1 unknown (year 1 mission 2)
};

// Team loadout record: far 5178:0000, 4 x 12 bytes (Point Man, OIC, Corpsman,
// Rear Security at +0x00/+0x0C/+0x18/+0x24), 0xFF terminator at 5178:0030.
struct LoadoutRecord {
    s8 se_id;                // +0x00 SE number, -1 = end of list
    u8 camouflage;           // +0x01 SE+0x62
    s8 weapons[4];           // +0x02 WeaponId slots A..D, -1 empty
    u8 reloads[4];           // +0x06 magazines for slots A..D
    s8 tools[2];             // +0x0A ItemType slots E, F, -1 empty
};

// Difficulty options: far 5178:0148, 8 words = st1.dfr (shipped 1,2,2,2,1,1,1,1).
struct DifficultyOptions {
    u16 ammo;                // +0x00 (5178:0148) 0 Unlimited, 1 Real (reloads consume magazines)
    u16 enemy_wounds;        // +0x02 0 Death, 1 Heavy, 2 Real
    u16 intelligence;        // +0x04 0 Minimal, 1 Decreased, 2 Real (noise_hear thresholds)
    u16 player_wounds;       // +0x06 0 None, 1 Decreased, 2 Real
    u16 reload_time;         // +0x08 0 Instant (0x40 ticks), 1 Timed (weapon table)
    u16 team_size;           // +0x0A 0 Decreased, 1 Real, 2 Enhanced (1 and 2 behave alike)
    u16 weapons;             // +0x0C 0 Unlimited, 1 Real
    u16 map;                 // +0x0E 0 Freeze (world paused on the map), 1 Real
};
constexpr std::size_t kDifficultySize = 16;

// s.cnf: 0xE8 bytes, image of far 5178:0060..0147.
struct ConfigFile {
    u8 unk_00[3];            // +0x00 (5178:0060) no reader
    s8 last_slot;            // +0x03 last used campaign slot, -1 none (disables "Continue")
    u8 slot_used[8];         // +0x04
    char names[8][26];       // +0x0C campaign names
    u8 unk_dc[12];           // +0xDC (5178:013C) no reader
};
constexpr std::size_t kConfigSize = 0xE8;

// World file <world>.w record (worlds.lib), 18 bytes; up to 524 per world.
struct WorldFileRecord {
    u8 heading_code;         // +0x00 degrees = 2h, +1 if (2h mod 15) != 0
    u8 unk_01;               // +0x01
    s32 x;                   // +0x02 map units
    s32 z;                   // +0x06
    u8 type;                 // +0x0A model table index
    u8 unk_0b;               // +0x0B
    u16 flags;               // +0x0C -> WorldObject::flags
    u16 hit_points;          // +0x0E -> WorldObject::hit_points
    u8 cover;                // +0x10 -> WorldObject::cover (overrides by kind/type)
    u8 height;               // +0x11 -> WorldObject::height
};
constexpr std::size_t kWorldRecordSize = 18;
constexpr std::size_t kWorldMaxObjects = 524;

// World description <world>.wd, 64 bytes, loaded to DS:ECB8.
struct WorldDesc {
    u8 unk_00;               // +0x00 0 in all files
    char area_name[63];      // +0x01 g_area_name (DS:ECB9), NUL terminated (stale bytes follow)
};
constexpr std::size_t kWorldDescSize = 64;

// Bull-session chatter sealNN.s: 6 records of 84 bytes.
struct SealChatterLine {
    char text[80];           // +0x00 NUL padded
    u8 year_from;            // +0x50 date window, all 0xFF = always
    u8 tier_from;            // +0x51 flow-table stage
    u8 year_to;              // +0x52
    u8 tier_to;              // +0x53
};
constexpr std::size_t kSealChatterSize = 84;

// Fixed-width text resources (.S): cYmNN.s 24 x 80, hYmNN.s 28 x 80,
// c.s (credits) and c0..c5.s 80-byte lines.
constexpr std::size_t kTextLineLen = 80;

// ---------------------------------------------------------------------------
// Parse / serialize (byte-exact, little endian). parse* return false when the
// size is wrong (the original treats a wrong MCI size as fatal).
// ---------------------------------------------------------------------------

bool parseMci(const u8* data, std::size_t size, MciHeader& out);
void serializeMci(const MciHeader& in, std::vector<u8>& out);  // 0x176 bytes

bool parseMtm(const u8* data, std::size_t size, std::vector<MtmTeam>& out);  // size / 0x66 records, <= 15
void serializeMtm(const std::vector<MtmTeam>& in, std::vector<u8>& out);

bool parseSe(const u8* data, std::size_t size, SeRecord& out);  // 0x90 bytes
void serializeSe(const SeRecord& in, std::vector<u8>& out);

// cN.cmp: header (0x14C) + n x 0x18 roster entries (n = (size - 0x14C) / 0x18, max 40).
bool parseCampaign(const u8* data, std::size_t size, CampaignHeader& header, std::vector<RosterEntry>& roster);
void serializeCampaign(const CampaignHeader& header, const std::vector<RosterEntry>& roster, std::vector<u8>& out);

bool parseConfig(const u8* data, std::size_t size, ConfigFile& out);  // s.cnf, 0xE8 bytes
void serializeConfig(const ConfigFile& in, std::vector<u8>& out);

bool parseDifficulty(const u8* data, std::size_t size, DifficultyOptions& out);  // st1.dfr, 16 bytes
void serializeDifficulty(const DifficultyOptions& in, std::vector<u8>& out);

// .S files: split into fixed-width lines (NUL padded; text ends at the first NUL).
std::vector<std::string> parseTextLines(const u8* data, std::size_t size, std::size_t line_len = kTextLineLen);
void serializeTextLines(const std::vector<std::string>& lines, std::size_t line_len, std::vector<u8>& out);
bool parseSealChatter(const u8* data, std::size_t size, std::vector<SealChatterLine>& out);  // 6 x 84
void serializeSealChatter(const std::vector<SealChatterLine>& in, std::vector<u8>& out);

bool parseWorld(const u8* data, std::size_t size, std::vector<WorldFileRecord>& out);  // .w
void serializeWorld(const std::vector<WorldFileRecord>& in, std::vector<u8>& out);
bool parseWorldDesc(const u8* data, std::size_t size, WorldDesc& out);  // .wd

} // namespace st::game
