# Events

Events are in-game occurrences that characters may react to. When an event fires, each eligible character rolls to decide whether they respond — the chance depends on the event type and is further reduced by a decaying penalty after each successful roll (configured via `RollPenaltyOnAnswer`).

| Event | When it triggers | Example |
|---|---|---|
| **Whisper received** | A character receives a private whisper from another player. | `John tells you privately: How are you doing?` |
| **Chat message received** | A character hears a say, yell, or group message. If the message **mentions** specific characters by name, those characters roll at `ReplyChanceMention`; non-mentioned bystanders roll at a reduced chance. | `John says: It was a nice fight, huh?` |
| **Guild chat received** | A character hears a guild chat message. Mention-aware rolling works the same way as regular chat messages; can be disabled via `EnableGuildChat`. | `John says in guild chat: Anyone up for SM?` |
| **Guild chat received** | A character hears a guild chat message. Mention-aware rolling works the same way as regular chat messages; can be disabled via `EnableGuildChat`. | `John says in guild chat: Anyone up for SM?` |
| **Party found item** | A party member acquires a rare (blue) or higher weapon or armor — via loot, quest reward, or group roll. Quest rewards are tracked for the party leader only to avoid duplicates (the whole party receives the same item). | `*The party has found a legendary two-handed mace named Bane of the Damned*` |
| **Character won duel** | A party member wins a duel. | `*John just won the duel against Joe*` |
| **Character leveled up** | A party member reaches every 5th level (5, 10, 15 …). Other level-ups are ignored. | `*John can feel their abilities growing stronger*` |
| **Significant combat ended** | A significant combat encounter ends (polled every 1s, debounced — requires `PBC.CombatEndDebounceCycles` consecutive cycles with no combat). A combat is significant if any notable enemy was killed, at least one party member died, or 10+ enemies were slain. A preliminary LLM call generates a one-line narrative summary. | `*After a grueling fight against VanCleef and his lieutenants in the Deadmines, the party stood victorious — though just barely, with several members heavily wounded.*` |
| **Quest taken** | The party leader accepts a quest from an NPC, game object, or item. A preliminary LLM call generates a one-line narrative summary. | `*The party has agreed to help Farmer Fung with his troubles*` |
| **Quest completed** | The party leader completes a quest. A preliminary LLM call generates a one-line narrative summary. | `*The party has delivered the supplies to Crossroads*` |
| **Party flight started** | All party members are in flight at the same time (polled every 1 s). | `*The party has started a flight to Crossroads*` |
| **Party location changed** | All party members share the same zone and it differs from the last tracked zone. The new zone must remain stable for `PBC.LocationChangeDebounceCycles` consecutive cycles before the event fires. Sub-zone changes within the same zone do not trigger this. Location checks are skipped while in flight. | `*Party has arrived in Elwynn Forest*` |
| **Trigger** | Fired manually via the `.chars trigger` command or the `POST /api/char/:guid/trigger` API endpoint. The character always responds (no roll). The event is **not** written into the character's history. Response is sent as a raid message if the character is in a raid group, a party message if in a regular party, or a say otherwise. The event line is picked dynamically based on the character's last history message. | `*you feel the urge to say something*` (default) |

### Time-gap narrator line

When more than 5 minutes pass between consecutive history entries for a character, a narrator line is automatically inserted to indicate the time gap:

> `Narrator: *some time passes*`

This line is not inserted if both the last and current messages are private (whisper) messages between the same two characters.
