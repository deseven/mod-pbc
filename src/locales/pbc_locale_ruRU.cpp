#include "pbc_locales.h"

// ---------------------------------------------------------------------------
// Russian (ruRU) translations for prompt variable strings
//
// Each entry maps the English string (used as the lookup key in source code)
// to its Russian equivalent.
// ---------------------------------------------------------------------------

static const PBC_LocaleMap ruRU = {
    // ---- Time of day -------------------------------------------------
    { "early night",     "ранняя ночь" },
    { "night",           "ночь" },
    { "late night",      "поздняя ночь" },
    { "early morning",   "раннее утро" },
    { "morning",         "утро" },
    { "late morning",    "позднее утро" },
    { "noon",            "полдень" },
    { "afternoon",       "день" },
    { "late afternoon",  "поздний день" },
    { "early evening",   "ранний вечер" },
    { "late evening",    "поздний вечер" },

    // ---- Weather clauses ---------------------------------------------
    { "the weather is fine",              "погода хорошая" },
    { "it's foggy",                       "туманно" },
    { "it's raining lightly",             "идёт лёгкий дождь" },
    { "it's raining",                     "идёт дождь" },
    { "it's raining heavily",             "идёт сильный дождь" },
    { "it's snowing lightly",             "идёт лёгкий снег" },
    { "it's snowing",                     "идёт снег" },
    { "it's snowing heavily",             "идёт сильный снег" },
    { "there is a light sandstorm",       "лёгкая песчаная буря" },
    { "there is a sandstorm",             "песчаная буря" },
    { "there is a heavy sandstorm",       "сильная песчаная буря" },
    { "there is a thunderstorm",          "гроза" },

    // ---- Time / weather assembly -------------------------------------
    { "it's ",                            "сейчас " },
    { " and ",                            " и " },
    { ", but you are inside and sheltered from the weather",
      ", но ты внутри и [m:укрыт|f:укрыта] от непогоды" },

    // ---- Scene templates (travel state) ------------------------------
    // {0} = destination / mount name / place, {1} = time+weather
    { "You are currently flying to {0}, {1}.",
      "Ты сейчас летишь в {0}, {1}." },
    { "You are currently flying, {0}.",
      "Ты сейчас в полёте, {0}." },
    // {0} = mount name, {1} = place, {2} = time+weather
    { "You are currently flying {0} in {1}, {2}.",
      "Ты сейчас летишь на {0} в {1}, {2}." },
    // {0} = mount name, {1} = place, {2} = time+weather
    { "You are currently riding {0} in {1}, {2}.",
      "Ты сейчас едешь на {0} в {1}, {2}." },
    // {0} = place, {1} = time+weather
    { "You are currently riding a mount in {0}, {1}.",
      "Ты сейчас едешь верхом в {0}, {1}." },
    // {0} = place, {1} = time+weather
    { "You are currently on foot in {0}, {1}.",
      "Ты сейчас на ногах в {0}, {1}." },

    // ---- Place name fallback -----------------------------------------
    { "Unknown", "Неизвестно" },

    // ---- Combat status -----------------------------------------------
    { "You are not currently in combat.", "Ты сейчас не в бою." },
    // {0} = enemy name
    { "You are currently fighting {0}.",  "Ты сейчас сражаешься с {0}." },
    { "You are currently in combat.",     "Ты сейчас в бою." },

    // ---- Roles -------------------------------------------------------
    { "tank",         "танк" },
    { "melee DPS",    "ближний бой" },
    { "ranged DPS",   "дальний бой" },
    { "DPS", "урон" },
    { "support", "поддержка" },
    { "Your combat role is {0}.", "Ваша роль в бою — {0}." },
    { "{0} named {1}", "{0} по имени {1}" },
    { "Your {0} is seriously wounded.", "Ваш спутник {0} тяжело ранен." },
    { "Your {0} is by your side.", "Ваш спутник {0} рядом с вами." },
    { "Your shadowhound is seriously wounded.", "Ваша теневая гончая тяжело ранена." },
    { "You currently have no shadowhound at your side.", "Сейчас рядом с вами нет теневой гончей." },
    { "You currently have no summoned companion at your side.", "Сейчас рядом с вами нет призванного спутника." },
    { "{0} ({1}'s companion, seriously wounded)", "{0} (спутник персонажа {1}, тяжело ранен)" },
    { "{0} ({1}'s companion)", "{0} (спутник персонажа {1})" },
    { "healer",       "лекарь" },
    { "paladin",      "паладин" },
    { "death knight", "рыцарь смерти" },
    { "shaman",       "шаман" },
    { "druid",        "друид" },
    { "adventurer",   "искатель приключений" },

    // ---- Demon type names --------------------------------------------
    { "imp",        "бес" },
    { "voidwalker", "демон Бездны" },
    { "succubus",   "суккуб" },
    { "felhunter",  "охотник Скверны" },
    { "felguard",   "страж Скверны" },
    { "demon",      "демон" },

    // ---- Pet: not capable --------------------------------------------
    { "You currently don't know how to tame or call a pet.",
      "Ты пока не умеешь приручать или призывать питомца." },
    { "You currently don't know how to summon a demon.",
      "Ты пока не умеешь призывать демона." },

    // ---- Pet: no pet out ---------------------------------------------
    { "You currently have no pet at your side.",
      "Рядом с тобой сейчас нет питомца." },
    { "You currently have no demon at your side.",
      "Рядом с тобой сейчас нет демона." },
    { "You currently have no risen ghoul at your side.",
      "Рядом с тобой сейчас нет восставшего вурдалака." },
    { "You currently have no water elemental at your side.",
      "Рядом с тобой сейчас нет элементаля воды." },

    // ---- Pet: alive --------------------------------------------------
    // {0} = family / demon type, {1} = pet name
    { "Your {0} {1} is by your side, happy and alert.",
      "Твой питомец {0} {1} рядом с тобой и отлично себя чувствует." },
    // {0} = family / demon type, {1} = pet name
    { "Your {0} {1} is by your side, content.",
      "Твой питомец {0} {1} рядом с тобой и в удовлетворительном состоянии." },
    // {0} = family / demon type, {1} = pet name
    { "Your {0} {1} is by your side, but seems unhappy.",
      "Твой питомец {0} {1} рядом с тобой, но выглядит недовольно." },
    // {0} = demon type, {1} = pet name
    { "Your {0} {1} is by your side.",
      "Твой питомец {0} {1} рядом с тобой." },
    // {0} = ghoul name
    { "Your risen ghoul {0} is by your side.",
      "Твой восставший вурдалак {0} рядом с тобой." },
    { "Your water elemental is by your side.",
      "Твой элементаль воды рядом с тобой." },

    // ---- Pet: dead ---------------------------------------------------
    // {0} = family, {1} = pet name
    { "Your {0} {1} is seriously wounded.",
      "Твой питомец {0} {1} при смерти." },

    // ---- Group member pet snippets -----------------------------------
    // {0} = family / demon type, {1} = pet name, {2} = owner name
    { "{0} {1} ({2}'s pet)",
      "{0} {1} (питомец {2})" },
    // {0} = family, {1} = pet name, {2} = owner name
    { "{0} {1} ({2}'s pet, seriously wounded)",
      "{0} {1} (питомец {2}, при смерти)" },
    // {0} = demon type, {1} = pet name, {2} = owner name
    { "{0} {1} ({2}'s demon)",
      "{0} {1} (призван {2})" },
    // {0} = pet name, {1} = owner name
    { "{0} ({1}'s risen ghoul)",
      "{0} (восставший вурдалак {1})" },
    // {0} = owner name
    { "Water Elemental ({0}'s summon)",
      "Элементаль Воды (призван {0})" },

    // ---- Pet family fallback -----------------------------------------
    { "pet", "питомец" },

    // ---- Group status ------------------------------------------------
    { "You are not currently in a group.",
      "Ты сейчас не в группе." },
    { "You are currently in a group",
      "Ты сейчас в группе" },
    // {0} = member list string
    { "You are currently in a group with the following members: {0}",
      "Ты сейчас в группе со следующими участниками: {0}" },
    // {0} = leader info string
    { "You are currently in a group led by {0}",
      "Ты сейчас в группе под руководством {0}" },
    // {0} = leader info, {1} = member list
    { "You are currently in a group led by {0} with the following members: {1}",
      "Ты сейчас в группе под руководством {0} со следующими участниками: {1}" },

    // ---- Natural list (LOS) ------------------------------------------
    { "You see ",  "Ты видишь " },
    { " nearby.",  " неподалёку." },

    // ---- Default relationship ----------------------------------------
    // {0} = target character name
    { "You don't know much about {0}.",
      "Ты мало знаешь о {0}." },

    // ---- Class names -------------------------------------------------
    { "Warrior",      "Воин" },
    { "Paladin",      "Паладин" },
    { "Hunter",       "Охотник" },
    { "Rogue",        "Разбойник" },
    { "Priest",       "Жрец" },
    { "Death Knight", "Рыцарь смерти" },
    { "Shaman",       "Шаман" },
    { "Mage",         "Маг" },
    { "Warlock",      "Чернокнижник" },
    { "Druid",        "Друид" },
    { "Adventurer",   "Искатель приключений" },

    // ---- Race names --------------------------------------------------
    { "Human",     "Человек" },
    { "Orc",       "Орк" },
    { "Dwarf",     "Дворф" },
    { "Night Elf", "Ночной эльф" },
    { "Forsaken",  "Отрекшийся" },
    { "Tauren",    "Таурен" },
    { "Gnome",     "Гном" },
    { "Troll",     "Тролль" },
    { "Blood Elf", "Эльф крови" },
    { "Draenei",   "Дреней" },

    // ---- Gender ------------------------------------------------------
    { "male",   "мужчина" },
    { "female", "женщина" },

    // ---- Equipment: armour quality adjectives ------------------------
    { "simple",      "простое" },
    { "modest",      "скромное" },
    { "fine",        "хорошее" },
    { "excellent",   "отличное" },
    { "exceptional", "исключительное" },

    // ---- Equipment: armour materials ---------------------------------
    { "cloth",   "ткани" },
    { "leather", "кожи" },
    { "mail",    "кольчуги" },
    { "plate",   "лат" },

    // ---- Equipment: description templates ----------------------------
    { "You have no armor.",
      "У тебя нет брони." },
    // {0} = quality adjective (e.g. "хорошее", "отличное")
    { "You have {0} equipment.",
      "У тебя {0} снаряжение." },
    // {0} = quality adjective, {1} = material name
    { "You have {0} equipment made of {1}.",
      "У тебя {0} снаряжение из {1}." },

    // -- Weapon lines ({0}=rarity omitted in Russian — adjectives are gendered) --
    { "Your main weapon is a {0} {1} called {2}.",
      "Твоё основное оружие — {1} {2}." },
    { "Your main weapon is a {0}.",
      "Твоё основное оружие — {0}." },
    // -- Dual-wield combined line ("Your main weapons are … and …") --
    { "Your main weapons are {0} and {1}.",
      "Твоё оружие — {0} и {1}." },
    // -- Standalone weapon item phrases (no sentence wrapper, no period) --
    { "a {0} {1} called {2}",
      "{1} {2}" },
    { "a {0}",
      "{0}" },
    { "Your ranged weapon is a {0} {1} called {2}.",
      "Твоё оружие дальнего боя — {1} {2}." },
    { "Your ranged weapon is a {0}.",
      "Твоё оружие дальнего боя — {0}." },

    // ---- Equipment: off-hand types -----------------------------------
    { "off-hand item",  "предмет" },
    { "off-hand focus", "фокус" },

    // ---- Bag space ---------------------------------------------------
    { "Your bags are about half full.",
      "Твои сумки заполнены примерно наполовину." },
    { "Your bags are getting full.",
      "Твои сумки тяжелеют." },
    { "Your bags are almost full.",
      "Твои сумки почти полны." },
    { "Your bags are nearly full.",
      "Твои сумки практически полны." },
    { "Your bags are completely full.",
      "Твои сумки полностью заполнены." },

    // ---- Item quality names (PBC_ItemQualityStr) ---------------------
    { "common",     "обычный" },
    { "uncommon",   "необычный" },
    { "rare",       "редкий" },
    { "epic",       "эпический" },
    { "legendary",  "легендарный" },
    { "artifact",   "артефактный" },
    { "heirloom",   "фамильный" },

    // ---- Weapon types ------------------------------------------------
    { "one-handed axe",  "одноручный топор" },
    { "two-handed axe",  "двуручный топор" },
    { "bow",             "лук" },
    { "gun",             "ружьё" },
    { "one-handed mace", "одноручная булава" },
    { "two-handed mace", "двуручная булава" },
    { "polearm",         "древковое" },
    { "one-handed sword","одноручный меч" },
    { "two-handed sword","двуручный меч" },
    { "staff",           "посох" },
    { "fist weapon",     "кистевое" },
    { "dagger",          "кинжал" },
    { "thrown weapon",   "метательное" },
    { "crossbow",        "арбалет" },
    { "wand",            "жезл" },
    { "spear",           "копьё" },
    { "weapon",          "оружие" },

    // ---- Armor slot names --------------------------------------------
    { "helm",         "шлем" },
    { "necklace",     "ожерелье" },
    { "shoulders",    "наплечники" },
    { "shirt",        "рубашка" },
    { "chest armor",  "нагрудник" },
    { "belt",         "пояс" },
    { "legguards",    "поножи" },
    { "boots",        "сапоги" },
    { "bracers",      "наручи" },
    { "gloves",       "перчатки" },
    { "ring",         "кольцо" },
    { "trinket",      "аксессуар" },
    { "cloak",        "плащ" },
    { "tabard",       "накидка" },
    { "robe",         "роба" },
    { "shield",       "щит" },
    { "relic",        "реликвия" },
    { "armor",        "броня" },

    // ---- Armor sub-types (shields / relics) --------------------------
    { "buckler", "баклер" },
    { "libram",  "либрам" },
    { "idol",    "идол" },
    { "totem",   "тотем" },
    { "sigil",   "печать" },

    // ---- Fallback phrases --------------------------------------------
    { "an item",     "предмет" },
    { "item",        "предмет" },

    // ---- Quest giver / ender types -----------------------------------
    { "person",           "личность" },
    { "object",           "предмет" },
    { "person or object", "личность или предмет" },

    // ---- Combat toughness --------------------------------------------
    { "The party confidently disposed of the enemies.",
      "Отряд уверенно расправился с врагами." },
    { "The party members suffered minor wounds.",
      "Члены отряда получили лёгкие ранения." },
    { "The party members suffered major wounds.",
      "Члены отряда получили серьёзные ранения." },
    { "The party was almost wiped out and barely survived.",
      "Отряд был почти уничтожен и едва выжил." },

    // ---- Combat enemies section labels -------------------------------
    { "Regular enemies defeated: ",
      "Обычных врагов побеждено: " },
    { "none",
      "нет" },
    { "Significant enemies defeated: ",
      "Значимых врагов побеждено: " },

    // ---- Combat duration ---------------------------------------------
    { "short",     "короткий" },
    { "average",   "средний" },
    { "long",      "долгий" },
    { "very long", "очень долгий" },

    // ---- Chat history rendering (pbc_character.cpp) ------------------
    // {0} = narrator message text
    { "Narrator: *{0}*",
      "Рассказчик: *{0}*" },
    // {0} = character name — narrator event line for pending reactions
    { "{0} thinks...",
      "{0} думает..." },
    // {0} = message text — the character's own chat message
    { "You: {0}",
      "Ты: {0}" },
    // {0} = target name, {1} = message text
    { "You (privately to {0}): {1}",
      "Ты (лично {0}): {1}" },
    // {0} = message text
    { "You (privately): {0}",
      "Ты (лично): {0}" },
    // {0} = sender name, {1} = message text
    { "{0} (privately to you): {1}",
      "{0} (лично тебе): {1}" },
    // {0} = sender name, {1} = target name, {2} = message text
    { "{0} (privately to {1}): {2}",
      "{0} (лично {1}): {2}" },
    // {0} = sender name, {1} = message text
    { "{0} (privately): {1}",
      "{0} (лично): {1}" },
    // {0} = sender name, {1} = message text (regular chat)
    { "{0}: {1}",
      "{0}: {1}" },

    // ---- Time gap ----------------------------------------------------
    { "some time passes",
      "проходит некоторое время" },

    // ---- Whisper event lines (pbc_event_dispatch.cpp) ----------------
    // {0} = sender name, {1} = message
    { "{0} tells you privately: {1}",
      "{0} говорит тебе лично: {1}" },
    // {0} = sender name, {1} = message
    { "{0} says: {1}",
      "{0} говорит: {1}" },

    // ---- Flight / location narrator texts (pbc_poll.cpp) -------------
    // {0} = destination name
    { "The party has started a flight to {0}",
      "Отряд начал полёт в {0}" },
    // {0} = destination name
    { "The party started a flight to {0}",
      "Отряд отправился в полёт в {0}" },
    // {0} = zone name
    { "Party has arrived in {0}",
      "Отряд прибыл в {0}" },
    // {0} = zone name
    { "Party moved to {0}",
      "Отряд переместился в {0}" },

    // ---- Level up phrases (pbc_player_scripts.cpp) -------------------
    // Event (present tense)
    { " grows stronger",
      " становится сильнее" },
    { " becomes more powerful through experience",
      " набирается могущества с опытом" },
    { " emerges from their trials more capable than before",
      " выходит из испытаний сильнее и искуснее прежнего" },
    { " feels their abilities sharpen and grow",
      " чувствует, как [m:его|f:её] способности обостряются и растут" },
    { " gains new strength and skill",
      " обретает новую силу и умение" },
    // History (past tense)
    { " grew stronger",
      " [m:стал|f:стала] сильнее" },
    { " became more powerful through experience",
      " [m:набрался|f:набралась] могущества с опытом" },
    { " emerged from their trials more capable than before",
      " [m:вышел|f:вышла] из испытаний сильнее и искуснее прежнего" },
    { " felt their abilities sharpen and grow",
      " [m:почувствовал|f:почувствовала], как [m:его|f:её] способности обостряются и растут" },
    { " gained new strength and skill",
      " [m:обрёл|f:обрела] новую силу и умение" },

    // ---- Trigger event lines (pbc_event_dispatch.cpp) ----------------
    { "you want to comment on your surroundings",
      "ты хочешь прокомментировать окружение" },
    { "you want to ask a question",
      "ты хочешь задать вопрос" },
    { "you want to share something",
      "ты хочешь чем-то поделиться" },
    { "you want to comment on how you feel",
      "ты хочешь прокомментировать своё самочувствие" },
    { "you feel the urge to comment on the last thing that happened",
      "ты чувствуешь желание прокомментировать последнее событие" },
    { "you feel like saying more",
      "ты чувствуешь, что хочешь сказать больше" },
    { "you feel like answering that",
      "ты чувствуешь, что хочешь ответить" },
    { "you feel the urge to say something",
      "ты чувствуешь желание что-то сказать" },

    // ---- Relationship prompt line (pbc_character.cpp) ------------------
    // {0} = target name, {1} = relationship description
    { "Your relationship with {0}: {1}",
      "Твои отношения с {0}: {1}" },

    // ---- Narrator progress messages (pbc_event_processor.cpp) -----------
    // {0} = character name
    { "Condensing {0}'s history...",
      "Сжатие истории персонажа {0}..." },
    // {0} = character name, {1} = target name
    { "Updating {0}'s relationship with {1}...",
      "Обновление отношений {0} с {1}..." },

    // ---- Duel event lines (pbc_player_scripts.cpp) ----------------------
    // {0} = winner name, {1} = loser name
    { "{0} just won the duel against {1}",
      "{0} только что [m:победил|f:победила] в дуэли против {1}" },
    { "{0} won the duel against {1}",
      "{0} [m:победил|f:победила] в дуэли против {1}" },

    // ---- Item phrase template (PBC_BuildItemPhrase) ------------------
    // {0}=quality, {1}=item type.  Uses a fixed-gender anchor word
    // ("предмет", masc.) to carry the quality adjective safely, then
    // appends the actual type after a dash.
    { "a {0} {1}", "{0} предмет — {1}" },

    // ---- Item found / rewarded (pbc_player_scripts.cpp) --------------
    // {0} = item phrase (e.g. "an epic sword"), {1} = item name
    { "The party has found {0} named {1}",
      "Отряд нашёл {0} {1}" },
    // {0} = item phrase, {1} = item name
    { "The party acquired {0} named {1}",
      "Отряд получил {0} {1}" },
    // {0} = item phrase, {1} = item name
    { "The party has been rewarded with {0} named {1}",
      "Отряд получил в награду {0} {1}" },
    // {0} = item phrase, {1} = item name
    { "The party was rewarded with {0} named {1}",
      "Отряд получил в награду {0} {1}" },

    // ---- Fallback names ----------------------------------------------
    { "Unknown Item", "Неизвестный предмет" },
};

const PBC_LocaleMap* PBC_GetLocaleMap_ruRU()
{
    return &ruRU;
}
