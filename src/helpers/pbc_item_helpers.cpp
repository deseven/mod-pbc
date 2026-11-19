#include "pbc_item_helpers.h"
#include "pbc_locales.h"
#include <ctime>

#include "ItemTemplate.h"
#include "SharedDefines.h"

// ---------------------------------------------------------------------------
// Quality
// ---------------------------------------------------------------------------

std::string PBC_ItemQualityStr(uint32 quality)
{
    switch (quality)
    {
        case ITEM_QUALITY_UNCOMMON: return PBC_Localize("uncommon");
        case ITEM_QUALITY_RARE:     return PBC_Localize("rare");
        case ITEM_QUALITY_EPIC:     return PBC_Localize("epic");
        case ITEM_QUALITY_LEGENDARY:return PBC_Localize("legendary");
        case ITEM_QUALITY_ARTIFACT: return PBC_Localize("artifact");
        case ITEM_QUALITY_HEIRLOOM: return PBC_Localize("heirloom");
        default:                    return PBC_Localize("rare");
    }
}

// ---------------------------------------------------------------------------
// Weapon
// ---------------------------------------------------------------------------

std::string PBC_WeaponTypeStr(uint32 subClass)
{
    switch (subClass)
    {
        case ITEM_SUBCLASS_WEAPON_AXE:      return PBC_Localize("one-handed axe");
        case ITEM_SUBCLASS_WEAPON_AXE2:     return PBC_Localize("two-handed axe");
        case ITEM_SUBCLASS_WEAPON_BOW:      return PBC_Localize("bow");
        case ITEM_SUBCLASS_WEAPON_GUN:      return PBC_Localize("gun");
        case ITEM_SUBCLASS_WEAPON_MACE:     return PBC_Localize("one-handed mace");
        case ITEM_SUBCLASS_WEAPON_MACE2:    return PBC_Localize("two-handed mace");
        case ITEM_SUBCLASS_WEAPON_POLEARM:  return PBC_Localize("polearm");
        case ITEM_SUBCLASS_WEAPON_SWORD:    return PBC_Localize("one-handed sword");
        case ITEM_SUBCLASS_WEAPON_SWORD2:   return PBC_Localize("two-handed sword");
        case ITEM_SUBCLASS_WEAPON_STAFF:    return PBC_Localize("staff");
        case ITEM_SUBCLASS_WEAPON_FIST:     return PBC_Localize("fist weapon");
        case ITEM_SUBCLASS_WEAPON_DAGGER:   return PBC_Localize("dagger");
        case ITEM_SUBCLASS_WEAPON_THROWN:   return PBC_Localize("thrown weapon");
        case ITEM_SUBCLASS_WEAPON_CROSSBOW: return PBC_Localize("crossbow");
        case ITEM_SUBCLASS_WEAPON_WAND:     return PBC_Localize("wand");
        case ITEM_SUBCLASS_WEAPON_SPEAR:    return PBC_Localize("spear");
        default:                            return PBC_Localize("weapon");
    }
}

// ---------------------------------------------------------------------------
// Armor
// ---------------------------------------------------------------------------

std::string PBC_ArmorSlotStr(uint32 inventoryType)
{
    switch (inventoryType)
    {
        case INVTYPE_HEAD:       return PBC_Localize("helm");
        case INVTYPE_NECK:       return PBC_Localize("necklace");
        case INVTYPE_SHOULDERS:  return PBC_Localize("shoulders");
        case INVTYPE_BODY:       return PBC_Localize("shirt");
        case INVTYPE_CHEST:      return PBC_Localize("chest armor");
        case INVTYPE_WAIST:      return PBC_Localize("belt");
        case INVTYPE_LEGS:       return PBC_Localize("legguards");
        case INVTYPE_FEET:       return PBC_Localize("boots");
        case INVTYPE_WRISTS:     return PBC_Localize("bracers");
        case INVTYPE_HANDS:      return PBC_Localize("gloves");
        case INVTYPE_FINGER:     return PBC_Localize("ring");
        case INVTYPE_TRINKET:    return PBC_Localize("trinket");
        case INVTYPE_CLOAK:      return PBC_Localize("cloak");
        case INVTYPE_TABARD:     return PBC_Localize("tabard");
        case INVTYPE_ROBE:       return PBC_Localize("robe");
        case INVTYPE_HOLDABLE:   return PBC_Localize("off-hand item");
        case INVTYPE_SHIELD:     return PBC_Localize("shield");
        case INVTYPE_RELIC:      return PBC_Localize("relic");
        default:                 return PBC_Localize("armor");
    }
}

std::string PBC_BuildArmorTypeStr(uint32 subClass, uint32 inventoryType)
{
    // Shields, bucklers and relics have their own distinct names
    switch (subClass)
    {
        case ITEM_SUBCLASS_ARMOR_SHIELD:  return PBC_Localize("shield");
        case ITEM_SUBCLASS_ARMOR_BUCKLER: return PBC_Localize("buckler");
        case ITEM_SUBCLASS_ARMOR_LIBRAM:  return PBC_Localize("libram");
        case ITEM_SUBCLASS_ARMOR_IDOL:    return PBC_Localize("idol");
        case ITEM_SUBCLASS_ARMOR_TOTEM:   return PBC_Localize("totem");
        case ITEM_SUBCLASS_ARMOR_SIGIL:   return PBC_Localize("sigil");
        default: break;
    }

    // Get the slot name from inventory type
    std::string slot = PBC_ArmorSlotStr(inventoryType);

    // Cloth / leather / mail / plate: combine material + slot
    switch (subClass)
    {
        case ITEM_SUBCLASS_ARMOR_CLOTH:   return PBC_Localize("cloth") + " " + slot;
        case ITEM_SUBCLASS_ARMOR_LEATHER: return PBC_Localize("leather") + " " + slot;
        case ITEM_SUBCLASS_ARMOR_MAIL:    return PBC_Localize("mail") + " " + slot;
        case ITEM_SUBCLASS_ARMOR_PLATE:   return PBC_Localize("plate") + " " + slot;
        default: break;
    }

    // Misc subclass: just use the slot name (rings, trinkets, cloaks, etc.)
    return slot;
}

// ---------------------------------------------------------------------------
// Composite phrase builder
//
// Only ITEM_CLASS_WEAPON and ITEM_CLASS_ARMOR can reach this function
// (the loot/quest-reward events are filtered by PBC_LOOT_EVENT_ITEM_CLASSES).
// Everything else falls back to a generic "a {quality} item".
// ---------------------------------------------------------------------------

std::string PBC_BuildItemPhrase(ItemTemplate const* tmpl)
{
    if (!tmpl) return PBC_Localize("an item");

    std::string quality = PBC_ItemQualityStr(tmpl->Quality);
    std::string type;

    switch (tmpl->Class)
    {
        case ITEM_CLASS_WEAPON: type = PBC_WeaponTypeStr(tmpl->SubClass); break;
        case ITEM_CLASS_ARMOR:  type = PBC_BuildArmorTypeStr(tmpl->SubClass, tmpl->InventoryType); break;
        default:                type = PBC_Localize("item"); break;
    }

    return PBC_Localize("a {0} {1}", quality, type);
}
