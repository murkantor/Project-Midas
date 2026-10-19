/* Native setup conversion adapted from akratch/mgb64 (MIT).
 * See tools/THIRD_PARTY_NOTICES.txt for attribution. */
#ifdef GE_NATIVE_64
#include <ultra64.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bondtypes.h"
#include "memp.h"
#include "game/prop.h"

static void *setupAlloc(size_t count, size_t bytes)
{
    void *p = mempAllocBytesInBank(count * bytes, MEMPOOL_STAGE);
    if (p == NULL) abort();
    memset(p, 0, count * bytes);
    return p;
}
static inline u32 bswap32(u32 v) {
    return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
           ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000);
}

static inline f32 be_float(const u8 *p) {
    u32 v = bswap32(*(const u32 *)p);
    f32 f;
    memcpy(&f, &v, 4);
    return f;
}

static inline u16 be_u16(const u8 *p) {
    return (u16)((p[0] << 8) | p[1]);
}

static inline s16 be_s16(const u8 *p) {
    return (s16)be_u16(p);
}

static inline u32 be_u32(const u8 *p) {
    return bswap32(*(const u32 *)p);
}

static inline s32 be_s32(const u8 *p) {
    return (s32)be_u32(p);
}

#define N64_OBJECT_RECORD_BYTES             0x80
#define N64_MONITOR_RECORD_BYTES            0x74
#define N64_MONITOR_OWNER_OFFSET_OFFSET     0xF4
#define N64_MONITOR_OWNER_PART_OFFSET       0xF8
#define N64_MONITOR_IMAGE_NUM_OFFSET        0xFC
#define N64_MULTI_MONITOR_IMAGE_NUMS_OFFSET (N64_OBJECT_RECORD_BYTES + (N64_MONITOR_RECORD_BYTES * 4))

static s32 sizepropdef_n64_bytes(u8 type) {
    switch (type) {
        case PROPDEF_GUARD:         return 28;
        case PROPDEF_DOOR:          return 256;
        case PROPDEF_DOOR_SCALE:    return 8;
        case PROPDEF_PROP:          return 128;
        case PROPDEF_GLASS:         return 128;
        case PROPDEF_TINTED_GLASS:  return 0x25 * 4;
        case PROPDEF_SAFE:          return 0x20 * 4;
        case PROPDEF_GAS_RELEASING: return 0x20 * 4;
        case PROPDEF_KEY:           return 0x21 * 4;
        case PROPDEF_ALARM:         return 0x20 * 4;
        case PROPDEF_CCTV:          return 0x3b * 4;
        case PROPDEF_MAGAZINE:      return 0x21 * 4;
        case PROPDEF_COLLECTABLE:   return 0x22 * 4;
        case PROPDEF_MONITOR:       return 0x40 * 4;
        case PROPDEF_MULTI_MONITOR: return 0x95 * 4;
        case PROPDEF_RACK:          return 0x20 * 4;
        case PROPDEF_AUTOGUN:       return 0x36 * 4;
        case PROPDEF_LINK:          return 3 * 4;
        case PROPDEF_HAT:           return 0x20 * 4;
        case PROPDEF_GUARD_ATTRIBUTE: return 3 * 4;
        case PROPDEF_SWITCH:        return 4 * 4;
        case PROPDEF_SAFE_ITEM:     return 5 * 4;
        case PROPDEF_AMMO:          return 0x2d * 4;
        case PROPDEF_ARMOUR:        return 0x22 * 4;
        case PROPDEF_TAG:           return 4 * 4;
        case PROPDEF_RENAME:        return 10 * 4;
        case PROPDEF_OBJECTIVE_START:  return 4 * 4;
        case PROPDEF_OBJECTIVE_END:    return 1 * 4;
        case PROPDEF_OBJECTIVE_DESTROY_OBJECT:      return 2 * 4;
        case PROPDEF_OBJECTIVE_COMPLETE_CONDITION:   return 2 * 4;
        case PROPDEF_OBJECTIVE_FAIL_CONDITION:       return 2 * 4;
        case PROPDEF_OBJECTIVE_COLLECT_OBJECT:       return 2 * 4;
        case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT:       return 2 * 4;
        case PROPDEF_OBJECTIVE_PHOTOGRAPH:           return 4 * 4;
        case PROPDEF_OBJECTIVE_NULL:                 return 1 * 4;
        case PROPDEF_OBJECTIVE_ENTER_ROOM:           return 4 * 4;
        case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT_IN_ROOM: return 5 * 4;
        case PROPDEF_OBJECTIVE_COPY_ITEM:            return 1 * 4;
        case PROPDEF_WATCH_MENU_OBJECTIVE_TEXT:       return 4 * 4;
        case PROPDEF_LOCK_DOOR:     return 4 * 4;
        case PROPDEF_VEHICHLE:      return 0x2c * 4;
        case PROPDEF_AIRCRAFT:      return 0x2d * 4;
        case PROPDEF_TANK:          return 0x38 * 4;
        case PROPDEF_CAMERAPOS:     return 7 * 4;
        case PROPDEF_UNK41:         return 0x20 * 4;
        case PROPDEF_END:           return 4;
        default:                    return 4;
    }
}

static inline void propdef_convert_header(const u8 *src, PropDefHeaderRecord *dst) {
    dst->extrascale = be_u16(src + 0);
    dst->state = src[2];
    dst->type = src[3];
}

static void propdef_convert_objectrecord(const u8 *n64, ObjectRecord *pc) {
    memset(pc, 0, sizeof(ObjectRecord));

    pc->extrascale = be_u16(n64 + 0);
    pc->state = n64[2];
    pc->type = n64[3];
    pc->obj = be_s16(n64 + 4);
    pc->pad = be_s16(n64 + 6);
    pc->flags = be_u32(n64 + 8);
    pc->flags2 = be_u32(n64 + 12);

    pc->prop = NULL;
    pc->model = NULL;

    for (s32 i = 0; i < 16; i++) {
        ((f32 *)&pc->mtx)[i] = be_float(n64 + 0x18 + i * 4);
    }

    pc->runtime_pos.f[0] = be_float(n64 + 0x58);
    pc->runtime_pos.f[1] = be_float(n64 + 0x5C);
    pc->runtime_pos.f[2] = be_float(n64 + 0x60);

    pc->runtime_bitflags = be_u32(n64 + 0x64);

    pc->ptr_allocated_collisiondata_block = NULL;
    pc->projectile = NULL;

    pc->maxdamage = be_float(n64 + 0x70);
    pc->damage = be_float(n64 + 0x74);

    memcpy(&pc->shadecol, n64 + 0x78, 4);
    memcpy(&pc->nextcol, n64 + 0x7C, 4);
}

s32 romfileNativePropSize(u8 type) {
    switch (type) {
        case PROPDEF_GUARD:         return (s32)sizeof(GuardRecord);
        case PROPDEF_DOOR:          return (s32)sizeof(DoorRecord);
        case PROPDEF_DOOR_SCALE:    return (s32)sizeof(GlobalDoorScaleRecord);
        case PROPDEF_PROP:          return (s32)sizeof(ObjectRecord);
        case PROPDEF_GLASS:         return (s32)sizeof(ObjectRecord);
        case PROPDEF_COLLECTABLE:   return (s32)sizeof(WeaponObjRecord);
        case PROPDEF_KEY:           return (s32)sizeof(KeyRecord);
        case PROPDEF_TINTED_GLASS:  return (s32)sizeof(TintedGlassRecord);
        case PROPDEF_ARMOUR:        return (s32)sizeof(BodyArmourRecord);
        case PROPDEF_MONITOR:       return (s32)sizeof(MonitorObjRecord);
        case PROPDEF_MULTI_MONITOR: return (s32)sizeof(MultiMonitorObjRecord);
        case PROPDEF_AUTOGUN:       return (s32)sizeof(AutogunRecord);
        case PROPDEF_AMMO:          return (s32)sizeof(MultiAmmoCrateRecord);
        case PROPDEF_VEHICHLE:      return (s32)sizeof(VehichleRecord);
        case PROPDEF_AIRCRAFT:      return (s32)sizeof(AircraftRecord);
        case PROPDEF_TANK:          return (s32)sizeof(TankRecord);
        case PROPDEF_CCTV:          return (s32)sizeof(CCTVRecord);
        case PROPDEF_MAGAZINE:      return (s32)sizeof(AmmoCrateRecord);
        case PROPDEF_SAFE_ITEM:     return (s32)sizeof(SafeObjectRecord);

        case PROPDEF_SAFE:          return (s32)sizeof(ObjectRecord);
        case PROPDEF_GAS_RELEASING: return (s32)sizeof(ObjectRecord);
        case PROPDEF_ALARM:         return (s32)sizeof(ObjectRecord);
        case PROPDEF_RACK:          return (s32)sizeof(ObjectRecord);
        case PROPDEF_HAT:           return (s32)sizeof(ObjectRecord);
        case PROPDEF_UNK41:         return (s32)sizeof(ObjectRecord);

        case PROPDEF_TAG:           return (s32)sizeof(TagObjectRecord);
        case PROPDEF_RENAME:        return (s32)sizeof(RenameObjectRecord);
        case PROPDEF_LINK:          return (s32)sizeof(LinkRecord);
        case PROPDEF_SWITCH:        return (s32)sizeof(SwitchRecord);
        case PROPDEF_LOCK_DOOR:     return (s32)sizeof(LockDoorRecord);
        case PROPDEF_WATCH_MENU_OBJECTIVE_TEXT: return (s32)sizeof(struct setup_objective_text);
        case PROPDEF_OBJECTIVE_ENTER_ROOM:     return (s32)sizeof(struct criteria_roomentered);
        case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT_IN_ROOM: return (s32)sizeof(struct criteria_deposit);
        case PROPDEF_OBJECTIVE_PHOTOGRAPH:     return (s32)sizeof(struct criteria_picture);

        case PROPDEF_GUARD_ATTRIBUTE: return 16;
        case PROPDEF_CAMERAPOS:     return ((s32)sizeof(CutsceneRecord) + 7) & ~7;
        case PROPDEF_OBJECTIVE_START:  return (s32)sizeof(struct objective_entry);
        case PROPDEF_OBJECTIVE_END:    return 8;
        case PROPDEF_OBJECTIVE_DESTROY_OBJECT:      return 2 * 4;
        case PROPDEF_OBJECTIVE_COMPLETE_CONDITION:   return 2 * 4;
        case PROPDEF_OBJECTIVE_FAIL_CONDITION:       return 2 * 4;
        case PROPDEF_OBJECTIVE_COLLECT_OBJECT:       return 2 * 4;
        case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT:       return 2 * 4;
        case PROPDEF_OBJECTIVE_NULL:                 return 8;
        case PROPDEF_OBJECTIVE_COPY_ITEM:            return 8;
        case PROPDEF_END:           return 8;
        default:                    return 8;
    }
}

static PropDefHeaderRecord *propdef_convert_n64_to_pc(const u8 *n64_data) {
    const u8 *src;
    u8 *dst;
    u8 type;
    s32 n64_size, pc_size;
    s32 total_pc_size = 0;
    s32 record_count = 0;

    src = n64_data;
    for (;;) {
        type = src[3];
        n64_size = sizepropdef_n64_bytes(type);
        pc_size = romfileNativePropSize(type);
        total_pc_size += pc_size;
        record_count++;
        if (type == PROPDEF_END) break;
        src += n64_size;
        if (record_count > 10000) {
            fprintf(stderr, "[SETUP-PC] ERROR: propDef record count overflow\n");
            return NULL;
        }
    }

    u8 *pc_buf = (u8 *)setupAlloc(1, total_pc_size);
    if (!pc_buf) return NULL;

    src = n64_data;
    dst = pc_buf;
    for (s32 rec = 0; rec < record_count; rec++) {
        type = src[3];
        n64_size = sizepropdef_n64_bytes(type);
        pc_size = romfileNativePropSize(type);

        switch (type) {

            case PROPDEF_PROP:
            case PROPDEF_GLASS:
            case PROPDEF_ALARM:
            case PROPDEF_HAT:
            case PROPDEF_SAFE:
            case PROPDEF_GAS_RELEASING:
            case PROPDEF_RACK:
            case PROPDEF_UNK41:
                propdef_convert_objectrecord(src, (ObjectRecord *)dst);
                break;

            case PROPDEF_GUARD: {
                GuardRecord *g = (GuardRecord *)dst;
                memset(g, 0, sizeof(GuardRecord));
                g->extrascale = be_u16(src + 0);
                g->state = src[2];
                g->type = src[3];
                g->chrnum = be_u16(src + 4);
                g->PadID = be_u16(src + 6);
                g->BodyID = be_u16(src + 8);
                g->AIListID = be_u16(src + 10);
                g->Preset = be_u16(src + 12);
                g->chrpreset1 = be_u16(src + 14);
                g->health = be_u16(src + 16);
                g->ReactionTime = be_u16(src + 18);
                g->bitflags = be_u16(src + 20);
                g->HeadID = be_s16(src + 22);
                g->Data = NULL;
                break;
            }

            case PROPDEF_DOOR: {
                DoorRecord *d = (DoorRecord *)dst;
                memset(d, 0, sizeof(DoorRecord));

                propdef_convert_objectrecord(src, (ObjectRecord *)d);

                d->linkedDoorOffset = be_s32(src + 0x80);
                d->maxFrac = be_float(src + 0x84);
                d->perimFrac = be_float(src + 0x88);
                d->accel = be_float(src + 0x8C);
                d->decel = be_float(src + 0x90);
                d->maxSpeed = be_float(src + 0x94);
                d->doorFlags = be_u16(src + 0x98);
                d->doorType = be_u16(src + 0x9A);
                d->keyflags = be_u32(src + 0x9C);
                d->autoCloseFrames = be_u32(src + 0xA0);
                d->doorOpenSound = be_u32(src + 0xA4);
                d->frac = be_float(src + 0xA8);
                d->unkac = be_float(src + 0xAC);
                d->unkb0 = be_float(src + 0xB0);
                d->openPosition = be_float(src + 0xB4);
                d->speed = be_float(src + 0xB8);
                d->openstate = (s8)src[0xBC];
                d->unkbd = src[0xBD];
                d->calculatedopacity = be_s16(src + 0xBE);
                d->TintDist = be_s32(src + 0xC0);
                d->CullDist = be_s16(src + 0xC4);
                d->soundType = (s8)src[0xC6];
                d->fadeTime60 = (s8)src[0xC7];

                d->linkedDoor = NULL;
                d->unkcc = NULL;

                d->bbox.ModelNumber = be_u32(src + 0xD0);
                d->bbox.Bounds.xmin = be_float(src + 0xD4);
                d->bbox.Bounds.xmax = be_float(src + 0xD8);
                d->bbox.Bounds.ymin = be_float(src + 0xDC);
                d->bbox.Bounds.ymax = be_float(src + 0xE0);
                d->bbox.Bounds.zmin = be_float(src + 0xE4);
                d->bbox.Bounds.zmax = be_float(src + 0xE8);
                d->openedTime = be_u32(src + 0xEC);
                d->portalNumber = be_s32(src + 0xF0);

                d->openSoundState = NULL;
                d->closeSoundState = NULL;
                d->lastcalc60i = be_s32(src + 0xFC);
                break;
            }

            case PROPDEF_DOOR_SCALE: {
                GlobalDoorScaleRecord *ds = (GlobalDoorScaleRecord *)dst;
                ds->extrascale = be_u16(src + 0);
                ds->state = src[2];
                ds->type = src[3];
                ds->Scale = be_s32(src + 4);
                break;
            }

            case PROPDEF_COLLECTABLE: {
                WeaponObjRecord *w = (WeaponObjRecord *)dst;
                memset(w, 0, sizeof(WeaponObjRecord));
                propdef_convert_objectrecord(src, (ObjectRecord *)w);
                w->weaponnum = (s8)src[0x80];
                w->LinkedWeaponType = (s8)src[0x81];
                w->timer = be_s16(src + 0x82);
                w->dualweapon = NULL;
                break;
            }

            case PROPDEF_AMMO: {
                MultiAmmoCrateRecord *m = (MultiAmmoCrateRecord *)dst;
                memset(m, 0, sizeof(MultiAmmoCrateRecord));
                propdef_convert_objectrecord(src, (ObjectRecord *)m);
                for (s32 i = 0; i < AMMOTYPE_GLOBAL_MAX; i++) {
                    m->slots[i].modelnum = be_u16(src + 0x80 + i * 4);
                    m->slots[i].quantity = be_u16(src + 0x82 + i * 4);
                }
                break;
            }

            case PROPDEF_KEY: {
                KeyRecord *k = (KeyRecord *)dst;
                memset(k, 0, sizeof(KeyRecord));
                propdef_convert_objectrecord(src, (ObjectRecord *)k);

                k->keyflags = be_u32(src + 0x80);
                break;
            }

            case PROPDEF_TINTED_GLASS: {
                TintedGlassRecord *tg = (TintedGlassRecord *)dst;
                s32 raw_unk90;

                memset(tg, 0, sizeof(TintedGlassRecord));
                propdef_convert_objectrecord(src, (ObjectRecord *)tg);
                tg->TintDist = be_s32(src + 0x80);
                tg->CullDist = be_s32(src + 0x84);
                tg->calculatedopacity = be_s32(src + 0x88);
                tg->portalnum = be_s32(src + 0x8C);
                raw_unk90 = be_s32(src + 0x90);
                memcpy(&tg->unk90, &raw_unk90, sizeof(raw_unk90));
                break;
            }

            case PROPDEF_CCTV: {
                CCTVRecord *c = (CCTVRecord *)dst;
                s32 raw_unkcc;
                s32 raw_unkd0;
                s32 raw_unkdc;
                s32 raw_unke8;

                memset(c, 0, sizeof(CCTVRecord));
                propdef_convert_objectrecord(src, (ObjectRecord *)c);
                c->lookpad = be_s32(src + 0x80);
                for (s32 i = 0; i < 16; i++) {
                    ((f32 *)&c->unk84)[i] = be_float(src + 0x84 + i * 4);
                }
                c->unkC4 = be_float(src + 0xC4);
                c->unkC8 = be_float(src + 0xC8);
                raw_unkcc = be_s32(src + 0xCC);
                raw_unkd0 = be_s32(src + 0xD0);
                memcpy(&c->unkCC, &raw_unkcc, sizeof(raw_unkcc));
                memcpy(&c->unkD0, &raw_unkd0, sizeof(raw_unkd0));
                c->unkD4 = be_s32(src + 0xD4);
                c->unkD8 = be_float(src + 0xD8);
                raw_unkdc = be_s32(src + 0xDC);
                memcpy(&c->unkDC, &raw_unkdc, sizeof(raw_unkdc));
                c->timer = be_s32(src + 0xE0);
                c->convert_to_f32 = be_s32(src + 0xE4);
                raw_unke8 = be_s32(src + 0xE8);
                memcpy(&c->unkE8, &raw_unke8, sizeof(raw_unke8));
                break;
            }

            case PROPDEF_ARMOUR: {
                BodyArmourRecord *ba = (BodyArmourRecord *)dst;
                memset(ba, 0, sizeof(BodyArmourRecord));
                propdef_convert_objectrecord(src, (ObjectRecord *)ba);
                ba->initialamount = be_float(src + 0x80);
                ba->amount = be_float(src + 0x84);
                break;
            }

            case PROPDEF_MONITOR: {
                MonitorObjRecord *m = (MonitorObjRecord *)dst;
                memset(m, 0, sizeof(MonitorObjRecord));
                propdef_convert_objectrecord(src, (ObjectRecord *)m);
                m->OwnerOffset = be_s32(src + N64_MONITOR_OWNER_OFFSET_OFFSET);
                m->OwnerPart = be_s32(src + N64_MONITOR_OWNER_PART_OFFSET);
                m->ImageNum = be_s32(src + N64_MONITOR_IMAGE_NUM_OFFSET);
                break;
            }

            case PROPDEF_MULTI_MONITOR: {
                MultiMonitorObjRecord *m = (MultiMonitorObjRecord *)dst;
                memset(m, 0, sizeof(MultiMonitorObjRecord));
                propdef_convert_objectrecord(src, (ObjectRecord *)m);
                memcpy(m->ImageNums, src + N64_MULTI_MONITOR_IMAGE_NUMS_OFFSET, sizeof(m->ImageNums));
                break;
            }

            case PROPDEF_TAG: {
                TagObjectRecord *t = (TagObjectRecord *)dst;

                propdef_convert_header(src, (PropDefHeaderRecord *)dst);
                t->ID = be_u16(src + 4);
                t->OffsetToObj = be_s16(src + 6);
                t->NextTag = NULL;
                t->TaggedObject = NULL;
                break;
            }

            case PROPDEF_LINK: {
                LinkRecord *l = (LinkRecord *)dst;
                propdef_convert_header(src, (PropDefHeaderRecord *)dst);
                l->Index1 = be_s32(src + 4);
                l->Index2 = be_s32(src + 8);
                l->next = NULL;
                break;
            }

            case PROPDEF_SWITCH: {
                SwitchRecord *s = (SwitchRecord *)dst;
                propdef_convert_header(src, (PropDefHeaderRecord *)dst);
                s->Index1 = be_s32(src + 4);
                s->Index2 = be_s32(src + 8);
                s->next = NULL;
                break;
            }

            case PROPDEF_LOCK_DOOR: {
                LockDoorRecord *ld = (LockDoorRecord *)dst;
                propdef_convert_header(src, (PropDefHeaderRecord *)dst);
                ld->Index1 = be_s32(src + 4);
                ld->Index2 = be_s32(src + 8);
                ld->next = NULL;
                break;
            }

            case PROPDEF_RENAME: {
                RenameObjectRecord *r = (RenameObjectRecord *)dst;
                propdef_convert_header(src, (PropDefHeaderRecord *)dst);
                r->TagID = be_s32(src + 4);
                r->unk08 = be_s32(src + 8);
                r->unk0c = be_s32(src + 12);
                r->unk10 = be_s32(src + 16);
                r->unk14 = be_s32(src + 20);
                r->unk18 = be_s32(src + 24);
                r->unk1c = be_s32(src + 28);
                r->unk20 = be_s32(src + 32);
                r->renobj = NULL;
                break;
            }

            case PROPDEF_WATCH_MENU_OBJECTIVE_TEXT: {
                struct setup_objective_text *ot = (struct setup_objective_text *)dst;
                propdef_convert_header(src, (PropDefHeaderRecord *)dst);
                ot->unk04 = be_s32(src + 4);
                ot->unk08 = be_u16(src + 8) | (be_u16(src + 10) << 16);
                ot->next = NULL;
                break;
            }

            case PROPDEF_SAFE_ITEM: {
                SafeObjectRecord *si = (SafeObjectRecord *)dst;
                propdef_convert_header(src, (PropDefHeaderRecord *)dst);
                si->Index1 = be_s32(src + 4);
                si->Index2 = be_s32(src + 8);
                si->Index3 = be_s32(src + 12);
                si->next = NULL;
                break;
            }

            case PROPDEF_OBJECTIVE_PHOTOGRAPH: {
                struct criteria_picture *cp = (struct criteria_picture *)dst;
                propdef_convert_header(src, (PropDefHeaderRecord *)dst);
                cp->tag_id = be_s32(src + 4);
                cp->flag = be_s32(src + 8);
                cp->next = NULL;
                break;
            }

            case PROPDEF_OBJECTIVE_ENTER_ROOM: {
                struct criteria_roomentered *cr = (struct criteria_roomentered *)dst;
                propdef_convert_header(src, (PropDefHeaderRecord *)dst);
                cr->pad = be_u32(src + 4);
                cr->status = be_u32(src + 8);
                cr->next = NULL;
                break;
            }

            case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT_IN_ROOM: {
                struct criteria_deposit *cd = (struct criteria_deposit *)dst;
                propdef_convert_header(src, (PropDefHeaderRecord *)dst);
                cd->weaponnum = be_s32(src + 4);
                cd->padid = be_s32(src + 8);
                cd->flag = be_s32(src + 12);
                cd->next = NULL;
                break;
            }

            case PROPDEF_GUARD_ATTRIBUTE:
            case PROPDEF_CAMERAPOS:
            case PROPDEF_OBJECTIVE_START:
            case PROPDEF_OBJECTIVE_END:
            case PROPDEF_OBJECTIVE_DESTROY_OBJECT:
            case PROPDEF_OBJECTIVE_COMPLETE_CONDITION:
            case PROPDEF_OBJECTIVE_FAIL_CONDITION:
            case PROPDEF_OBJECTIVE_COLLECT_OBJECT:
            case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT:
            case PROPDEF_OBJECTIVE_NULL:
            case PROPDEF_OBJECTIVE_COPY_ITEM: {

                propdef_convert_header(src, (PropDefHeaderRecord *)dst);
                for (s32 w = 1; w < n64_size / 4; w++) {
                    ((u32 *)dst)[w] = be_u32(src + w * 4);
                }
                break;
            }

            case PROPDEF_VEHICHLE: {
                VehichleRecord *v = (VehichleRecord *)dst;
                memset(v, 0, sizeof(VehichleRecord));
                propdef_convert_objectrecord(src, (ObjectRecord *)v);
                v->ailist = (AIRecord *)(uintptr_t)be_u32(src + 0x80);
                v->aioffset = be_u16(src + 0x84);
                v->aireturnlist = be_s16(src + 0x86);
                v->speed = be_float(src + 0x88);
                v->wheelxrot = be_float(src + 0x8C);
                v->wheelyrot = be_float(src + 0x90);
                v->speedaim = be_float(src + 0x94);
                v->speedtime60 = be_float(src + 0x98);
                v->turnrot60 = be_float(src + 0x9C);
                v->roty = be_float(src + 0xA0);
                v->path = NULL;
                v->nextstep = be_s32(src + 0xA8);
                v->Sound = NULL;
                break;
            }

            case PROPDEF_AIRCRAFT: {
                AircraftRecord *a = (AircraftRecord *)dst;
                memset(a, 0, sizeof(AircraftRecord));
                propdef_convert_objectrecord(src, (ObjectRecord *)a);
                a->ailist = (AIRecord *)(uintptr_t)be_u32(src + 0x80);
                a->aioffset = be_u16(src + 0x84);
                a->aireturnlist = be_s16(src + 0x86);
                a->rotoryrot = be_float(src + 0x88);
                a->rotaryspeed = be_float(src + 0x8C);
                a->rotaryspeedaim = be_float(src + 0x90);
                a->rotaryspeedtime = be_float(src + 0x94);
                a->speed = be_float(src + 0x98);
                a->speedaim = be_float(src + 0x9C);
                a->speedtime60 = be_float(src + 0xA0);
                a->yrot = be_float(src + 0xA4);
                a->nextstep = be_s32(src + 0xA8);
                a->path = NULL;
                a->Sound = NULL;
                break;
            }

            case PROPDEF_TANK: {
                TankRecord *t = (TankRecord *)dst;
                memset(t, 0, sizeof(TankRecord));
                propdef_convert_objectrecord(src, (ObjectRecord *)t);
                t->collision = NULL;

                for (s32 i = 0; i < 8; i++) {
                    t->rect.f[i] = be_float(src + 0x84 + i * 4);
                }

                t->unkA4 = be_s32(src + 0xA4);
                t->unkA8 = be_s32(src + 0xA8);
                t->unkAC = be_s32(src + 0xAC);
                t->unkB0 = be_s32(src + 0xB0);
                t->unkB4 = be_s32(src + 0xB4);
                t->unkB8 = be_s32(src + 0xB8);
                t->unkBC = be_s32(src + 0xBC);
                t->unkC0 = be_s32(src + 0xC0);
                t->is_firing_tank = be_s32(src + 0xC4);
                t->turret_vertical_angle = be_float(src + 0xC8);
                t->turret_orientation_angle = be_float(src + 0xCC);
                t->unkD0 = be_float(src + 0xD0);
                t->stan_y = be_float(src + 0xD4);
                t->unkD8 = be_s32(src + 0xD8);
                t->tank_orientation_angle = be_float(src + 0xDC);
                break;
            }

            case PROPDEF_AUTOGUN: {
                AutogunRecord *ag = (AutogunRecord *)dst;
                memset(ag, 0, sizeof(AutogunRecord));
                propdef_convert_objectrecord(src, (ObjectRecord *)ag);
                ag->padID = be_s32(src + 0x80);
                ag->rot_related = be_float(src + 0x84);
                ag->unk88 = be_float(src + 0x88);
                ag->unk8C = be_float(src + 0x8C);
                ag->unk90 = be_float(src + 0x90);
                ag->unk94 = be_float(src + 0x94);
                ag->unk98 = be_float(src + 0x98);
                ag->unk9C = be_float(src + 0x9C);
                ag->unkA0 = be_float(src + 0xA0);
                ag->speed = be_float(src + 0xA4);
                ag->aimdist = be_float(src + 0xA8);
                ag->unkAC = be_s32(src + 0xAC);
                ag->unkB0 = be_float(src + 0xB0);
                ag->unkB4 = be_float(src + 0xB4);
                ag->unkB8 = be_s32(src + 0xB8);
                ag->unkBC = be_s32(src + 0xBC);
                ag->unkC0 = be_s32(src + 0xC0);
                ag->unkC4 = NULL;
                ag->unkC8 = NULL;
                ag->beam = NULL;
                ag->is_active = be_s32(src + 0xD0);
                ag->unkD4 = be_float(src + 0xD4);
                break;
            }

            case PROPDEF_END: {
                PropDefHeaderRecord *e = (PropDefHeaderRecord *)dst;
                propdef_convert_header(src, e);
                break;
            }

            default: {
                if (n64_size >= 128) {

                    propdef_convert_objectrecord(src, (ObjectRecord *)dst);

                    s32 extra_n64_bytes = n64_size - 128;
                    s32 extra_start_pc = (s32)sizeof(ObjectRecord);
                    for (s32 w = 0; w < extra_n64_bytes / 4; w++) {
                        if (extra_start_pc + w * 4 + 4 <= pc_size) {
                            *(u32 *)(dst + extra_start_pc + w * 4) = be_u32(src + 128 + w * 4);
                        }
                    }
                } else {

                    propdef_convert_header(src, (PropDefHeaderRecord *)dst);
                    for (s32 w = 1; w < n64_size / 4 && w * 4 < pc_size; w++) {
                        ((u32 *)dst)[w] = be_u32(src + w * 4);
                    }
                }
                break;
            }
        }

        if (type == PROPDEF_OBJECTIVE_START) {
            struct objective_entry *objective = (void *)dst;
            objective->reserved = be_u16(src + 8);
            objective->text = be_u16(src + 10);
            objective->unkC = be_u16(src + 12);
            objective->unkD = src[14];
            objective->difficulty = src[15];
        }
        if (type == PROPDEF_GUARD_ATTRIBUTE) {
            u16 half = be_u16(src + 8);
            memcpy(dst + 8, &half, 2);
            memcpy(dst + 10, src + 10, 2);
        }
        src += n64_size;
        dst += pc_size;
    }

    return (PropDefHeaderRecord *)pc_buf;
}


static s32 *setupList(const u8 *base, u32 off)
{
    u32 n = 0, i;
    s32 *list;
    if (!off) return NULL;
    while (be_s32(base + off + n * 4) >= 0) n++;
    list = setupAlloc(n + 1, sizeof(*list));
    for (i = 0; i <= n; i++) list[i] = be_s32(base + off + i * 4);
    return list;
}

static pname *setupNames(const u8 *base, u32 off)
{
    u32 n = 0, i;
    pname *names;
    if (!off) return NULL;
    while (be_u32(base + off + n * 4)) n++;
    names = setupAlloc(n + 1, sizeof(*names));
    for (i = 0; i < n; i++) names[i].p = (char *)base + be_u32(base + off + i * 4);
    return names;
}

static s32 *setupIntro(const u8 *base, u32 off)
{
    static const u32 rawSizes[] = {12, 16, 16, 32, 8, 8, 40, 12, 8, 4};
    static const u32 nativeSizes[] = {
        sizeof(struct SetupIntroSpawn), sizeof(struct SetupIntroItem),
        sizeof(struct SetupIntroAmmo), sizeof(struct SetupIntroSwirl),
        sizeof(struct SetupIntroAnim), sizeof(struct SetupIntroCuff),
        sizeof(struct SetupIntroCamera), sizeof(struct SetupIntroWatch),
        sizeof(struct SetupIntroCredits), sizeof(struct SetupIntroEmpty)
    };
    const u8 *raw = base + off;
    u32 bytes = 0, type, i;
    u8 *result, *dst;
    if (!off) return NULL;
    do {
        type = be_u32(raw);
        if (type >= 10) abort();
        bytes += nativeSizes[type];
        raw += rawSizes[type];
    } while (type != INTROTYPE_END);
    result = dst = setupAlloc(1, bytes);
    raw = base + off;
    do {
        type = be_u32(raw);
        for (i = 0; i < rawSizes[type] && i < nativeSizes[type]; i += 4)
            *(u32 *)(dst + i) = be_u32(raw + i);
        if (type == INTROTYPE_CAMERA) {
            struct SetupIntroCamera *camera = (void *)dst;
            camera->lang1c.lang_index[0] = be_u32(raw + 28) & 0xffff;
            camera->lang20.lang_ptr = NULL;
            camera->lang20.lang_index = be_u32(raw + 32);
            camera->prev = NULL;
        } else if (type == INTROTYPE_CREDITS) {
            struct SetupIntroCredits *intro = (void *)dst;
            const u8 *credits = base + be_u32(raw + 4);
            u32 n = 0;
            CreditsEntry *converted;
            while (be_u16(credits + n * 12) || be_u16(credits + n * 12 + 2)) n++;
            converted = setupAlloc(n + 1, sizeof(*converted));
            for (i = 0; i < n * 6; i++) ((u16 *)converted)[i] = be_u16(credits + i * 2);
            intro->unk04 = (uintptr_t)converted;
        }
        *(u32 *)dst = type << 24;
        raw += rawSizes[type];
        dst += nativeSizes[type];
    } while (type != INTROTYPE_END);
    return (s32 *)result;
}

struct stagesetup *romfileSetupNative(const u8 *base)
{
    struct stagesetup *setup = setupAlloc(1, sizeof(*setup));
    u32 off, n, i, j;
    const u8 *raw;
    off = be_u32(base + 0);
    if (off) {
        raw = base + off;
        for (n = 0; be_s32(raw + n * 16) >= 0; n++) {}
        setup->pathwaypoints = setupAlloc(n + 1, sizeof(waypoint));
        for (i = 0; i < n; i++, raw += 16) {
            waypoint *wp = &setup->pathwaypoints[i];
            wp->padID = be_s32(raw);
            wp->neighbours = setupList(base, be_u32(raw + 4));
            wp->groupNum = be_s32(raw + 8);
            wp->dist = be_s32(raw + 12);
        }
        setup->pathwaypoints[n].padID = -1;
    }
    off = be_u32(base + 4);
    if (off) {
        raw = base + off;
        for (n = 0; be_u32(raw + n * 12); n++) {}
        setup->waypointgroups = setupAlloc(n + 1, sizeof(waygroup));
        for (i = 0; i < n; i++, raw += 12) {
            setup->waypointgroups[i].neighbours = setupList(base, be_u32(raw));
            setup->waypointgroups[i].waypoints = setupList(base, be_u32(raw + 4));
            setup->waypointgroups[i].dist = be_s32(raw + 8);
        }
    }
    setup->intro = setupIntro(base, be_u32(base + 8));
    off = be_u32(base + 12);
    if (off) setup->propDefs = propdef_convert_n64_to_pc(base + off);
    off = be_u32(base + 16);
    if (off) {
        raw = base + off;
        for (n = 0; be_u32(raw + n * 8); n++) {}
        setup->patrolpaths = setupAlloc(n + 1, sizeof(PathRecord));
        for (i = 0; i < n; i++, raw += 8) {
            setup->patrolpaths[i].waypoints = setupList(base, be_u32(raw));
            setup->patrolpaths[i].ID = raw[4];
            setup->patrolpaths[i].isLoop = raw[5];
        }
    }
    off = be_u32(base + 20);
    if (off) {
        raw = base + off;
        for (n = 0; be_u32(raw + n * 8); n++) {}
        setup->ailists = setupAlloc(n + 1, sizeof(AIListRecord));
        for (i = 0; i < n; i++, raw += 8) {
            setup->ailists[i].ailist = (AIRecord *)(base + be_u32(raw));
            setup->ailists[i].ID = be_s32(raw + 4);
        }
    }
    for (j = 0; j < 2; j++) {
        u32 stride = j ? 68 : 44;
        off = be_u32(base + 24 + j * 4);
        if (!off) continue;
        raw = base + off;
        for (n = 0; be_u32(raw + n * stride + 36); n++) {}
        if (j) setup->boundpads = setupAlloc(n + 1, sizeof(BoundPadRecord));
        else setup->pads = setupAlloc(n + 1, sizeof(PadRecord));
        for (i = 0; i < n; i++, raw += stride) {
            PadRecord *pad = j ? (PadRecord *)&setup->boundpads[i] : &setup->pads[i];
            for (u32 k = 0; k < 9; k++) ((f32 *)&pad->pos)[k] = be_float(raw + k * 4);
            pad->plink = (char *)base + be_u32(raw + 36);
            pad->stan = NULL;
            if (j) {
                BoundPadRecord *bp = &setup->boundpads[i];
                for (u32 k = 0; k < 6; k++) ((f32 *)&bp->bbox)[k] = be_float(raw + 44 + k * 4);
            }
        }
    }
    setup->padnames = setupNames(base, be_u32(base + 32));
    setup->boundpadnames = setupNames(base, be_u32(base + 36));
    return setup;
}
#endif
