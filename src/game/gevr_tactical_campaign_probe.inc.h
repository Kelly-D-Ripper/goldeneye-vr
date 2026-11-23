/* Inspect initialized campaign equipment against the original objective records.
 * Called only by the isolated --input-script --tactical-sim test path. */
static void gevrTacticalCampaignProbe(void)
{
    extern u8 *get_text_for_objective(int objectiveIndex);
    extern s32 get_difficulty_for_objective(s32 objectiveIndex);
    unsigned objectives = 0, missing = 0;

    for (int id = 0; id < objectiveGetCount() && id < OBJECTIVES_MAX; ++id) {
        const struct objective_entry *entry = objective_ptrs[id];
        if (!entry) continue;
        const char *text = (const char *)get_text_for_objective(id);
        sysLogPrintf(LOG_NOTE, "campaign audit: objective %d mask %d status %d text %.100s",
            id, get_difficulty_for_objective(id), get_status_of_objective(id), text ? text : "");
        for (const MissionObjectiveRecord *step = (const MissionObjectiveRecord *)&entry->id;
             step->type != PROPDEF_OBJECTIVE_END; step = (const MissionObjectiveRecord *)((const PropDefHeaderRecord *)step
                 + sizepropdef((PropDefHeaderRecord *)step))) {
            switch (step->type) {
            case PROPDEF_OBJECTIVE_DESTROY_OBJECT:
            case PROPDEF_OBJECTIVE_COLLECT_OBJECT:
            case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT:
            case PROPDEF_OBJECTIVE_PHOTOGRAPH: {
                const ObjectRecord *obj = objFindByTagId(step->ObjRefID);
                sysLogPrintf(LOG_NOTE, "campaign audit: target %d %d tag %u type %d model %d prop %d",
                    id, step->type, step->ObjRefID, obj ? obj->type : -1,
                    obj ? obj->obj : -1, obj && obj->prop);
                if (!obj) ++missing;
                break;
            }
            default:
                sysLogPrintf(LOG_NOTE, "campaign audit: criterion %d %d reference %u",
                    id, step->type, step->ObjRefID);
                break;
            }
        }
        ++objectives;
    }

    for (int item = ITEM_KNIFE; item < ITEM_IDS_MAX; ++item) {
        if (!bondinvHasInvItem(item)) continue;
        const WeaponStats *stats = get_ptr_item_statistics(item);
        const int ammo = stats ? stats->AmmoType : AMMO_NONE;
        sysLogPrintf(LOG_NOTE, "campaign audit: inventory %d ammo %d reserve %d", item, ammo,
            ammo > AMMO_NONE && ammo < AMMOTYPE_MAX ? g_CurrentPlayer->ammoheldarr[ammo] : 0);
    }

    for (PropDefHeaderRecord *def = g_CurrentSetup.propDefs; def && def->type != PROPDEF_END;
         def += sizepropdef(def)) {
        switch (def->type) {
        case PROPDEF_COLLECTABLE: {
            const WeaponObjRecord *weapon = (const WeaponObjRecord *)def;
            const PropRecord *prop = weapon->prop;
            if (weapon->weaponnum >= ITEM_BOMBCASE || weapon->weaponnum < ITEM_WPPK
                || !prop || !prop->parent) {
                sysLogPrintf(LOG_NOTE, "campaign audit: item %d pad %d model %d prop %d parent %d flags %x",
                    weapon->weaponnum, weapon->pad, weapon->obj, prop != NULL,
                    prop && prop->parent, weapon->flags);
            }
            break;
        }
        case PROPDEF_KEY: {
            const KeyRecord *key = (const KeyRecord *)def;
            sysLogPrintf(LOG_NOTE, "campaign audit: key pad %d flags %x prop %d parent %d",
                key->pad, key->keyflags, key->prop != NULL, key->prop && key->prop->parent);
            break;
        }
        case PROPDEF_MAGAZINE: {
            const AmmoCrateRecord *mag = (const AmmoCrateRecord *)def;
            sysLogPrintf(LOG_NOTE, "campaign audit: magazine pad %d ammo %d prop %d",
                mag->pad, mag->ammoType, mag->prop != NULL);
            break;
        }
        case PROPDEF_AMMO: {
            const MultiAmmoCrateRecord *crate = (const MultiAmmoCrateRecord *)def;
            for (int i = 0; i < AMMOTYPE_GLOBAL_MAX; ++i)
                if (crate->slots[i].quantity)
                    sysLogPrintf(LOG_NOTE, "campaign audit: crate pad %d ammo %d count %u prop %d",
                        crate->pad, i + 1, crate->slots[i].quantity, crate->prop != NULL);
            break;
        }
        case PROPDEF_TANK: {
            const TankRecord *tank = (const TankRecord *)def;
            sysLogPrintf(LOG_NOTE, "campaign audit: tank pad %d model %d shells %d prop %d",
                tank->pad, tank->obj, tank->unkD8, tank->prop != NULL);
            break;
        }
        default:
            break;
        }
    }
    sysLogPrintf(LOG_NOTE, "campaign audit: stage %d difficulty %d objectives %u missing tags %u",
        bossGetStageNum(), lvlGetSelectedDifficulty(), objectives, missing);
}
