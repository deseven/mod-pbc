#include "pbc_locales.h"

// ---------------------------------------------------------------------------
// German (deDE) translations for prompt variable strings
//
// Each entry maps the English string (used as the lookup key in source code)
// to its German equivalent.
// ---------------------------------------------------------------------------

static const PBC_LocaleMap deDE = {
    // ---- Time of day -------------------------------------------------
    { "early night",     "frühe Nacht" },
    { "night",           "Nacht" },
    { "late night",      "späte Nacht" },
    { "early morning",   "früher Morgen" },
    { "morning",         "Morgen" },
    { "late morning",    "später Morgen" },
    { "noon",            "Mittag" },
    { "afternoon",       "Nachmittag" },
    { "late afternoon",  "später Nachmittag" },
    { "early evening",   "früher Abend" },
    { "late evening",    "später Abend" },

    // ---- Weather clauses ---------------------------------------------
    { "the weather is fine",              "das Wetter ist schön" },
    { "it's foggy",                       "es ist neblig" },
    { "it's raining lightly",             "es regnet leicht" },
    { "it's raining",                     "es regnet" },
    { "it's raining heavily",             "es regnet stark" },
    { "it's snowing lightly",             "es schneit leicht" },
    { "it's snowing",                     "es schneit" },
    { "it's snowing heavily",             "es schneit stark" },
    { "there is a light sandstorm",       "es tobt ein leichter Sandsturm" },
    { "there is a sandstorm",             "es tobt ein Sandsturm" },
    { "there is a heavy sandstorm",       "es tobt ein schwerer Sandsturm" },
    { "there is a thunderstorm",          "es gewittert" },

    // ---- Time / weather assembly -------------------------------------
    { "it's ",                            "es ist " },
    { " and ",                            " und " },
    { ", but you are inside and sheltered from the weather",
      ", aber du bist drinnen und vor dem Wetter geschützt" },

    // ---- Scene templates (travel state) ------------------------------
    // {0} = destination / mount name / place, {1} = time+weather
    { "You are currently flying to {0}, {1}.",
      "Du fliegst gerade nach {0}, {1}." },
    { "You are currently flying, {0}.",
      "Du fliegst gerade, {0}." },
    // {0} = mount name, {1} = place, {2} = time+weather
    { "You are currently flying {0} in {1}, {2}.",
      "Du fliegst gerade {0} in {1}, {2}." },
    // {0} = mount name, {1} = place, {2} = time+weather
    { "You are currently riding {0} in {1}, {2}.",
      "Du reitest gerade {0} in {1}, {2}." },
    // {0} = place, {1} = time+weather
    { "You are currently riding a mount in {0}, {1}.",
      "Du reitest gerade ein Reittier in {0}, {1}." },
    // {0} = place, {1} = time+weather
    { "You are currently on foot in {0}, {1}.",
      "Du bist gerade zu Fuß in {0}, {1}." },

    // ---- Place name fallback -----------------------------------------
    { "Unknown", "Unbekannt" },

    // ---- Combat status -----------------------------------------------
    { "You are not currently in combat.", "Du bist gerade nicht in einem Kampf." },
    // {0} = enemy name
    { "You are currently fighting {0}.",  "Du kämpfst gerade gegen {0}." },
    { "You are currently in combat.",     "Du bist gerade in einem Kampf." },

    // ---- Roles -------------------------------------------------------
    { "tank",         "Tank" },
    { "melee DPS",    "Nahkampf-DPS" },
    { "ranged DPS",   "Fernkampf-DPS" },
    { "healer",       "Heiler" },
    { "paladin",      "Paladin" },
    { "death knight", "Todesritter" },
    { "shaman",       "Schamane" },
    { "druid",        "Druide" },
    { "adventurer",   "Abenteurer" },

    // ---- Demon type names --------------------------------------------
    { "imp",        "Wichtel" },
    { "voidwalker", "Leerwandler" },
    { "succubus",   "Sukkubus" },
    { "felhunter",  "Teufelsjäger" },
    { "felguard",   "Teufelswache" },
    { "demon",      "Dämon" },

    // ---- Pet: not capable --------------------------------------------
    { "You currently don't know how to tame or call a pet.",
      "Du weißt noch nicht, wie man ein Tier zähmt oder ruft." },
    { "You currently don't know how to summon a demon.",
      "Du weißt noch nicht, wie man einen Dämon beschwört." },

    // ---- Pet: no pet out ---------------------------------------------
    { "You currently have no pet at your side.",
      "Du hast gerade kein Tier an deiner Seite." },
    { "You currently have no demon at your side.",
      "Du hast gerade keinen Dämon an deiner Seite." },
    { "You currently have no risen ghoul at your side.",
      "Du hast gerade keinen auferstandenen Ghul an deiner Seite." },
    { "You currently have no water elemental at your side.",
      "Du hast gerade kein Wasserelementar an deiner Seite." },

    // ---- Pet: alive --------------------------------------------------
    // {0} = family / demon type, {1} = pet name
    { "Your {0} {1} is by your side, happy and alert.",
      "Dein(e) {0} {1} ist an deiner Seite, glücklich und aufmerksam." },
    // {0} = family / demon type, {1} = pet name
    { "Your {0} {1} is by your side, content.",
      "Dein(e) {0} {1} ist an deiner Seite, zufrieden." },
    // {0} = family / demon type, {1} = pet name
    { "Your {0} {1} is by your side, but seems unhappy.",
      "Dein(e) {0} {1} ist an deiner Seite, scheint aber unglücklich." },
    // {0} = demon type, {1} = pet name
    { "Your {0} {1} is by your side.",
      "Dein(e) {0} {1} ist an deiner Seite." },
    // {0} = ghoul name
    { "Your risen ghoul {0} is by your side.",
      "Dein auferstandener Ghul {0} ist an deiner Seite." },
    { "Your water elemental is by your side.",
      "Dein Wasserelementar ist an deiner Seite." },

    // ---- Pet: dead ---------------------------------------------------
    // {0} = family, {1} = pet name
    { "Your {0} {1} is seriously wounded.",
      "Dein(e) {0} {1} ist schwer verwundet." },

    // ---- Group member pet snippets -----------------------------------
    // {0} = family / demon type, {1} = pet name, {2} = owner name
    { "{0} {1} ({2}'s pet)",
      "{0} {1} ({2}s Tier)" },
    // {0} = family, {1} = pet name, {2} = owner name
    { "{0} {1} ({2}'s pet, seriously wounded)",
      "{0} {1} ({2}s Tier, schwer verwundet)" },
    // {0} = demon type, {1} = pet name, {2} = owner name
    { "{0} {1} ({2}'s demon)",
      "{0} {1} ({2}s Dämon)" },
    // {0} = pet name, {1} = owner name
    { "{0} ({1}'s risen ghoul)",
      "{0} ({1}s auferstandener Ghul)" },
    // {0} = owner name
    { "Water Elemental ({0}'s summon)",
      "Wasserelementar ({0}s Beschwörung)" },

    // ---- Pet family fallback -----------------------------------------
    { "pet", "Tier" },

    // ---- Group status ------------------------------------------------
    { "You are not currently in a group.",
      "Du bist gerade in keiner Gruppe." },
    { "You are currently in a group",
      "Du bist gerade in einer Gruppe" },
    // {0} = member list string
    { "You are currently in a group with the following members: {0}",
      "Du bist gerade in einer Gruppe mit folgenden Mitgliedern: {0}" },
    // {0} = leader info string
    { "You are currently in a group led by {0}",
      "Du bist gerade in einer Gruppe, angeführt von {0}" },
    // {0} = leader info, {1} = member list
    { "You are currently in a group led by {0} with the following members: {1}",
      "Du bist gerade in einer Gruppe, angeführt von {0}, mit folgenden Mitgliedern: {1}" },

    // ---- Natural list (LOS) ------------------------------------------
    { "You see ",  "Du siehst " },
    { " nearby.",  " in der Nähe." },

    // ---- Default relationship ----------------------------------------
    // {0} = target character name
    { "You don't know much about {0}.",
      "Du weißt nicht viel über {0}." },

    // ---- Class names -------------------------------------------------
    { "Warrior",      "Krieger" },
    { "Paladin",      "Paladin" },
    { "Hunter",       "Jäger" },
    { "Rogue",        "Schurke" },
    { "Priest",       "Priester" },
    { "Death Knight", "Todesritter" },
    { "Shaman",       "Schamane" },
    { "Mage",         "Magier" },
    { "Warlock",      "Hexenmeister" },
    { "Druid",        "Druide" },
    { "Adventurer",   "Abenteurer" },

    // ---- Race names --------------------------------------------------
    { "Human",     "Mensch" },
    { "Orc",       "Orc" },
    { "Dwarf",     "Zwerg" },
    { "Night Elf", "Nachtelf" },
    { "Forsaken",  "Verlassener" },
    { "Tauren",    "Tauren" },
    { "Gnome",     "Gnom" },
    { "Troll",     "Troll" },
    { "Blood Elf", "Blutelf" },
    { "Draenei",   "Draenei" },

    // ---- Gender ------------------------------------------------------
    { "male",   "männlich" },
    { "female", "weiblich" },

    // ---- Equipment: armour quality adjectives ------------------------
    { "simple",      "einfache" },
    { "modest",      "bescheidene" },
    { "fine",        "gute" },
    { "excellent",   "ausgezeichnete" },
    { "exceptional", "außergewöhnliche" },

    // ---- Equipment: armour materials ---------------------------------
    { "cloth",   "Stoff" },
    { "leather", "Leder" },
    { "mail",    "Kette" },
    { "plate",   "Platte" },

    // ---- Equipment: description templates ----------------------------
    { "You have no armor.",
      "Du hast keine Rüstung." },
    // {0} = quality adjective (e.g. "gute", "ausgezeichnete")
    { "You have {0} equipment.",
      "Du hast {0} Ausrüstung." },
    // {0} = quality adjective, {1} = material name
    { "You have {0} equipment made of {1}.",
      "Du hast {0} Ausrüstung aus {1}." },

    // -- Weapon lines ({0}=rarity omitted — German adjectives are gendered) --
    { "Your main weapon is a {0} {1} called {2}.",
      "Hauptwaffe: {1} namens {2}." },
    { "Your main weapon is a {0}.",
      "Hauptwaffe: {0}." },
    // -- Dual-wield combined line ("Your main weapons are … and …") --
    { "Your main weapons are {0} and {1}.",
      "Hauptwaffen: {0} und {1}." },
    // -- Standalone weapon item phrases (no sentence wrapper, no period) --
    { "a {0} {1} called {2}",
      "{1} namens {2}" },
    { "a {0}",
      "{0}" },
    { "Your ranged weapon is a {0} {1} called {2}.",
      "Fernkampfwaffe: {1} namens {2}." },
    { "Your ranged weapon is a {0}.",
      "Fernkampfwaffe: {0}." },

    // ---- Equipment: off-hand types -----------------------------------
    { "off-hand item",  "Gegenstand" },
    { "off-hand focus", "Fokus" },

    // ---- Bag space ---------------------------------------------------
    { "Your bags are about half full.",
      "Deine Taschen sind etwa halb voll." },
    { "Your bags are getting full.",
      "Deine Taschen werden voll." },
    { "Your bags are almost full.",
      "Deine Taschen sind fast voll." },
    { "Your bags are nearly full.",
      "Deine Taschen sind nahezu voll." },
    { "Your bags are completely full.",
      "Deine Taschen sind komplett voll." },

    // ---- Item quality names (PBC_ItemQualityStr) ---------------------
    { "common",     "gewöhnlichen" },
    { "uncommon",   "ungewöhnlichen" },
    { "rare",       "seltenen" },
    { "epic",       "epischen" },
    { "legendary",  "legendären" },
    { "artifact",   "einzigartigen" },
    { "heirloom",   "vererbten" },

    // ---- Weapon types ------------------------------------------------
    { "one-handed axe",  "Einhandaxt" },
    { "two-handed axe",  "Zweihandaxt" },
    { "bow",             "Bogen" },
    { "gun",             "Gewehr" },
    { "one-handed mace", "Einhandstreitkolben" },
    { "two-handed mace", "Zweihandstreitkolben" },
    { "polearm",         "Stangenwaffe" },
    { "one-handed sword","Einhandschwert" },
    { "two-handed sword","Zweihandschwert" },
    { "staff",           "Stab" },
    { "fist weapon",     "Faustwaffe" },
    { "dagger",          "Dolch" },
    { "thrown weapon",   "Wurfwaffe" },
    { "crossbow",        "Armbrust" },
    { "wand",            "Zauberstab" },
    { "spear",           "Speer" },
    { "weapon",          "Waffe" },

    // ---- Armor slot names --------------------------------------------
    { "helm",         "Helm" },
    { "necklace",     "Halskette" },
    { "shoulders",    "Schultern" },
    { "shirt",        "Hemd" },
    { "chest armor",  "Brustrüstung" },
    { "belt",         "Gürtel" },
    { "legguards",    "Beinschutz" },
    { "boots",        "Stiefel" },
    { "bracers",      "Armschienen" },
    { "gloves",       "Handschuhe" },
    { "ring",         "Ring" },
    { "trinket",      "Schmuckstück" },
    { "cloak",        "Umhang" },
    { "tabard",       "Wappenrock" },
    { "robe",         "Robe" },
    { "shield",       "Schild" },
    { "relic",        "Relikt" },
    { "armor",        "Rüstung" },

    // ---- Armor sub-types (shields / relics) --------------------------
    { "buckler", "Faustschild" },
    { "libram",  "Buchband" },
    { "idol",    "Götze" },
    { "totem",   "Totem" },
    { "sigil",   "Siegel" },

    // ---- Fallback phrases --------------------------------------------
    { "an item",     "ein Gegenstand" },
    { "item",        "Gegenstand" },

    // ---- Quest giver / ender types -----------------------------------
    { "person",           "Person" },
    { "object",           "Objekt" },
    { "person or object", "Person oder Objekt" },

    // ---- Combat toughness --------------------------------------------
    { "The party confidently disposed of the enemies.",
      "Die Gruppe hat die Gegner souverän besiegt." },
    { "The party members suffered minor wounds.",
      "Die Gruppenmitglieder erlitten leichte Verletzungen." },
    { "The party members suffered major wounds.",
      "Die Gruppenmitglieder erlitten schwere Verletzungen." },
    { "The party was almost wiped out and barely survived.",
      "Die Gruppe wurde fast ausgelöscht und überlebte nur knapp." },

    // ---- Combat enemies section labels -------------------------------
    { "Regular enemies defeated: ",
      "Besiegte gewöhnliche Gegner: " },
    { "none",
      "keine" },
    { "Significant enemies defeated: ",
      "Besiegte bedeutende Gegner: " },

    // ---- Combat duration ---------------------------------------------
    { "short",     "kurz" },
    { "average",   "durchschnittlich" },
    { "long",      "lang" },
    { "very long", "sehr lang" },

    // ---- Chat history rendering (pbc_character.cpp) ------------------
    // {0} = narrator message text
    { "Narrator: *{0}*",
      "Erzähler: *{0}*" },
    // {0} = character name — narrator event line for pending reactions
    { "{0} thinks...",
      "{0} denkt nach..." },
    // {0} = message text — the character's own chat message
    { "You: {0}",
      "Du: {0}" },
    // {0} = target name, {1} = message text
    { "You (privately to {0}): {1}",
      "Du (privat zu {0}): {1}" },
    // {0} = message text
    { "You (privately): {0}",
      "Du (privat): {0}" },
    // {0} = sender name, {1} = message text
    { "{0} (privately to you): {1}",
      "{0} (privat zu dir): {1}" },
    // {0} = sender name, {1} = target name, {2} = message text
    { "{0} (privately to {1}): {2}",
      "{0} (privat zu {1}): {2}" },
    // {0} = sender name, {1} = message text
    { "{0} (privately): {1}",
      "{0} (privat): {1}" },
    // {0} = sender name, {1} = message text (regular chat)
    { "{0}: {1}",
      "{0}: {1}" },

    // ---- Time gap ----------------------------------------------------
    { "some time passes",
      "einige Zeit vergeht" },

    // ---- Whisper event lines (pbc_event_dispatch.cpp) ----------------
    // {0} = sender name, {1} = message
    { "{0} tells you privately: {1}",
      "{0} sagt dir privat: {1}" },
    // {0} = sender name, {1} = message
    { "{0} says: {1}",
      "{0} sagt: {1}" },

    // ---- Flight / location narrator texts (pbc_poll.cpp) -------------
    // {0} = destination name
    { "The party has started a flight to {0}",
      "Die Gruppe hat einen Flug nach {0} begonnen" },
    // {0} = destination name
    { "The party started a flight to {0}",
      "Die Gruppe begann einen Flug nach {0}" },
    // {0} = zone name
    { "Party has arrived in {0}",
      "Die Gruppe ist in {0} angekommen" },
    // {0} = zone name
    { "Party moved to {0}",
      "Die Gruppe zog nach {0}" },

    // ---- Level up phrases (pbc_player_scripts.cpp) -------------------
    { " can feel their abilities growing stronger",
      " spürt, wie die eigenen Fähigkeiten stärker werden" },
    { " grows more powerful, their skills sharpened by experience",
      " wird mächtiger, die eigenen Fertigkeiten wurden durch Erfahrung geschärft" },
    { " has grown stronger through their trials and hardships",
      " ist durch Herausforderungen und Entbehrungen stärker geworden" },
    { " feels a surge of power as their abilities improve",
      " fühlt einen Energieschub, als sich die eigenen Fähigkeiten verbessern" },
    { " has become more capable, their experience forging them anew",
      " ist fähiger geworden, geprägt durch die eigene Erfahrung" },
    { " grew stronger",
      " wurde stärker" },
    { " became more powerful through experience",
      " wurde durch Erfahrung mächtiger" },
    { " emerged from their trials more capable than before",
      " ging aus den bestandenen Prüfungen fähiger hervor als zuvor" },
    { " felt their abilities sharpen and grow",
      " spürte, wie die eigenen Fähigkeiten wuchsen und sich schärften" },
    { " gained new strength and skill",
      " erlangte neue Stärke und Fertigkeit" },

    // ---- Trigger event lines (pbc_event_dispatch.cpp) ----------------
    { "you want to comment on your surroundings",
      "du möchtest deine Umgebung kommentieren" },
    { "you want to ask a question",
      "du möchtest eine Frage stellen" },
    { "you want to share something",
      "du möchtest etwas mitteilen" },
    { "you want to comment on how you feel",
      "du möchtest kommentieren, wie du dich fühlst" },
    { "you feel the urge to comment on the last thing that happened",
      "du verspürst den Drang, das letzte Geschehen zu kommentieren" },
    { "you feel like saying more",
      "du möchtest noch mehr sagen" },
    { "you feel like answering that",
      "du möchtest darauf antworten" },
    { "you feel the urge to say something",
      "du verspürst den Drang, etwas zu sagen" },

    // ---- Relationship prompt line (pbc_character.cpp) ------------------
    // {0} = target name, {1} = relationship description
    { "Your relationship with {0}: {1}",
      "Deine Beziehung zu {0}: {1}" },

    // ---- Narrator progress messages (pbc_event_processor.cpp) -----------
    // {0} = character name
    { "Condensing {0}'s history...",
      "Fasse den Verlauf von {0} zusammen..." },
    // {0} = character name, {1} = target name
    { "Updating {0}'s relationship with {1}...",
      "Aktualisiere die Beziehung von {0} zu {1}..." },

    // ---- Duel event lines (pbc_player_scripts.cpp) ----------------------
    // {0} = winner name, {1} = loser name
    { "{0} just won the duel against {1}",
      "{0} hat gerade das Duell gegen {1} gewonnen" },
    { "{0} won the duel against {1}",
      "{0} hat das Duell gegen {1} gewonnen" },

    // ---- Item phrase template (PBC_BuildItemPhrase) ------------------
    // {0}=quality (accusative masculine), {1}=item type.
    // All four event templates below use accusative so the adjective
    // form is consistent across every event.
    { "a {0} {1}", "einen {0} Gegenstand — {1}" },

    // ---- Item found / rewarded (pbc_player_scripts.cpp) --------------
    // {0} = item phrase, {1} = item name
    { "The party has found {0} named {1}",
      "Die Gruppe hat {0} namens {1} gefunden" },
    { "The party acquired {0} named {1}",
      "Die Gruppe hat {0} namens {1} erhalten" },
    // Rewarded templates use accusative ("… als Belohnung erhalten")
    // instead of dative ("mit … belohnt") so the item phrase keeps
    // the same adjective form across all four events.
    { "The party has been rewarded with {0} named {1}",
      "Die Gruppe hat {0} namens {1} als Belohnung erhalten" },
    { "The party was rewarded with {0} named {1}",
      "Die Gruppe hat {0} namens {1} als Belohnung erhalten" },

    // ---- Fallback names ----------------------------------------------
    { "Unknown Item", "Unbekannter Gegenstand" },
};

const PBC_LocaleMap* PBC_GetLocaleMap_deDE()
{
    return &deDE;
}
