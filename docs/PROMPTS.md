# Prompts

All prompt templates are loaded from files on disk. This allows the module to ship improved default prompts with every update without requiring users to manually sync their config.

The prompt directory is configured via `PBC.PromptsPath` in the config (default: `../../../modules/mod-pbc/prompts`).


## Locale Support

Prompt files are organised by locale subdirectories. The module automatically resolves the correct locale based on the server's `DBC.Locale` setting in `worldserver.conf`:

```
prompts/
├── enUS/
│   ├── Main.system.default.txt
│   ├── Main.system.custom.txt
│   └── ...
├── deDE/
│   ├── Main.system.custom.txt
│   └── ...
├── frFR/
└── ...
```

The module tries `<PBC.PromptsPath>/<DBC.Locale>/` first. If that directory doesn't exist, it falls back to `<PBC.PromptsPath>/enUS/`.

To localise your prompts, copy the files from `enUS/` into a new locale directory (e.g. `deDE/`), translate them, and reload the module.


## How It Works

For each prompt, the module first looks for a **custom** version (`.custom.txt`). If found, it is used. If not found, the **default** version (`.default.txt`) is loaded instead.

If any prompt fails to load (file not found, access error, etc.), the module is **disabled** — prompts are critical for operation.


## Customizing a Prompt

1. Find the default prompt file you want to customize in the prompts directory (e.g. `Main.system.default.txt`).
2. Copy it to a new file with `.custom.txt` extension instead of `.default.txt` (e.g. `Main.system.custom.txt`).
3. Edit the custom file as you see fit.
4. Reload the module with `.chars reload` in-game or from the server console.

Your custom file will not be overwritten by module updates. To revert to the default prompt, simply delete your `.custom.txt` file and reload.


## Prompt Files

| File | Description |
|------|-------------|
| `Main.system` | System prompt for the main character reply. Defines the character's behavior and response format. |
| `Main.user` | User prompt for the main character reply. Structures the character card, history, relationships, context, and event. |
| `Condensation.system` | System prompt for the condensation LLM call. Instructs the character to reflect on their recent experiences and write down the memories that matter to them personally, each with an importance score. |
| `Condensation.user` | User prompt for the condensation LLM call. Provides the character card, existing memories, and recent history for memory extraction. |
| `DefaultCharacterDescription` | Default character description used when no character card file exists. Supports basic template variables. |
| `CharacterContext` | Current context description appended to every prompt. Contains dynamic variables like location, combat status, etc. |
| `QuestCompleted.system` | System prompt for the quest completion summary LLM call. |
| `QuestCompleted.user` | User prompt for the quest completion summary. Supports quest-related template variables. |
| `QuestTaken.system` | System prompt for the quest taken summary LLM call. |
| `QuestTaken.user` | User prompt for the quest taken summary. Supports quest-related template variables. |
| `RelationshipUpdate.system` | System prompt for the relationship update LLM call. Produces a brief impression (1–2 short sentences) of how the character feels about another character. |
| `RelationshipUpdate.user` | User prompt for the relationship update. Supports relationship-related template variables. |
| `CombatEnded.system` | System prompt for the combat ended summary LLM call. Produces a one-line narrative summary of the fight. |
| `CombatEnded.user` | User prompt for the combat ended summary. Supports combat-related template variables. |


## Template Variables

Prompts and character cards support template variables enclosed in curly braces, e.g. `{char_name}`. Unknown variables (those that remain as `{token}` after substitution) are removed and a warning is logged.

### General Variables

These variables can be used in most prompts and character cards. It's recommended to only use things that change often (such as character level or character location) for `CharacterContext`.

- `{char_name}` — name of the character
- `{char_gender}` — gender of the character in game
- `{char_race}` — race of the character in game
- `{char_class}` — class of the character in game
- `{char_role}` — character role
- `{char_level}` — level of the character in game
- `{char_gold}` — amount of character's money
- `{scene}` — human-readable description of the character's current situation: travel state, location, time of day, and weather (if `mod_weather_vibe` is active). For example "You are currently on foot in Undercity, it's evening." or "You are currently riding Gray Kodo in The Barrens, it's noon." or "You are currently flying to Crossroads, it's morning." or "You are currently on foot in Gadgetzan (Tanaris), it's early evening and it's raining lightly."
- `{char_los}` — human-readable list of nearby characters and NPCs visible to the character, for example "You see John, Jane and Defias Bandit nearby." or "You see Defias Bandit nearby."
- `{combat_status}` — dynamic combat status, could be "You are not currently in combat." or "You are currently in combat.", or even "You are currently fighting Archimonde.", based on current target
- `{equipment}` — dynamic single-line equipment description. Rarity adjectives and weapon names are included only for rare+ quality items; below rare only the type is shown (e.g. "a dagger" not "a common dagger"). Main hand and off-hand are combined into one sentence when dual-wielding. When bags are at least ~40% full, a bag-space summary is also appended. Examples:
  - Full set (dual-wield): "You have fine equipment made of leather. Your main weapons are a rare dagger called Death's Sting and a dagger. Your ranged weapon is a rare bow called Heartstriker."
  - Two-hander: "You have excellent equipment made of plate. Your main weapon is an epic two-handed mace called Devastation. Your bags are almost full."
  - Bare minimum: "You have simple equipment. Your main weapon is a dagger."
  - No armor: "You have no armor. Your main weapon is a one-handed sword."
- `{pet_info}` — pet/summon status for the character themselves. Empty when the character is not a pet class or not capable. For example "Your wolf Fang is by your side, happy and alert." for a hunter, "Your succubus Alyxia is by your side." for a warlock, "Your risen ghoul Bonechewer is by your side." for a death knight, or "Your water elemental is by your side." for a mage.
- `{char_group}` — dynamic group status, could be "You are not currently in a group." or "You are currently in a group led by John (male Tauren Druid) with the following members: Jane (female Troll Rogue) and Kevin (male Blood Elf Paladin)." Pet info for other party members is appended comma-separated before the closing period, e.g. ", wolf Fang (Bob's pet), succubus Alyxia (Jane's pet)."

### Main Prompt Variables

These variables can only be used in the `Main.system` and `Main.user` prompts.

- `{character_card}` — current character card or generic description from `DefaultCharacterDescription`
- `{memories}` — discrete narrator-style memories extracted from previous condensations, selected by importance within the token budget (`PBC.MaxMemoriesCtx`). Output chronologically.
- `{chat_history}` — current chat history, including events
- `{relationships}` — the character's current relationship descriptions with other party members. When the character is not in a group with a real player (e.g. a whisper interaction), falls back to "You don't know much about <player_name>.". When in a group, lists one entry per member, e.g. "You know John is brave and kind." or "You don't know much about John." for members with no data yet. Updated automatically on condensation (manual or automatic) and via the `.chars relationship-update` command.
- `{context}` — current context for the character, defined in `CharacterContext`
- `{event}` — recently happened event, see [Events](EVENTS.md) for details

### Quest Prompt Variables

These variables can be used in the `QuestCompleted.user` and `QuestTaken.user` prompts.

- `{quest_title}` — the title of the quest
- `{quest_giver}` — the name of the NPC, game object, or item that offered the quest. For creature NPCs the gender is appended in parentheses (e.g. "Gryan Stoutmantle (male)").
- `{quest_giver_type}` — the type of the quest source: "person" (NPC/creature), "object" (world object like a signpost or bounty board), or "item" (carried item like a scroll or letter). Empty if unknown. For quest completed, may also be "person or object" if the quest has both creature and gameobject starters in the database.
- `{quest_ender}` — the name of the NPC or game object that completes the quest. For creature NPCs the gender is appended in parentheses (e.g. "Marshal Dughan (male)").
- `{quest_ender_type}` — the type of the quest ender: "person" (NPC/creature) or "object" (world object like a shrine or delivery point). Empty if unknown. May be "person or object" if the quest has both creature and gameobject enders in the database.
- `{quest_description}` — the full lore/details text of the quest (shown when accepting it)
- `{quest_log_description}` — the objectives text shown in the quest log
- `{quest_completion_log}` — the completion log text
- `{quest_reward_text}` — the NPC's reward speech: what the quest-giver says when handing out the reward upon turn-in

### Combat Prompt Variables

These variables can be used in the `CombatEnded.user` prompt.

- `{location}` — where the fight took place
- `{enemies_section}` — dynamically built enemy list. Examples:
  - Regular enemies only: `Regular enemies defeated: Defias Bandit x5, Scarab x3`
  - Significant enemies only: `Regular enemies defeated: none\nSignificant enemies defeated: VanCleef (Defias Kingpin)`
  - Both: `Regular enemies defeated: Defias Bandit x5\nSignificant enemies defeated: VanCleef (Defias Kingpin)`
- `{combat_toughness}` — narrative description of how tough the fight was, based on the ratio of party members who died:
  - No deaths: "The party confidently disposed of the enemies."
  - Up to 20% died: "The party members suffered minor wounds."
  - Up to 40% died: "The party members suffered major wounds."
  - More than 40% died: "The party was almost wiped out and barely survived."
- `{party_size}` — the number of party members when combat started
- `{combat_duration}` — generalized fight duration category: "short" (<30s), "average" (<60s), "long" (<150s), or "very long" (≥150s)
