# Installation

This guide covers installing `mod-pbc` on either stock [AzerothCore](https://www.azerothcore.org) or [Conquest of Azeroth](https://github.com/Conquest-of-AzerothCore/azerothcore-wotlk-coa) (CoA).

- [Common](#common) — relevant to both platforms: obtaining the sources, build notes, model connection and web app setup
- [AzerothCore](#azerothcore) — steps specific to stock AzerothCore
- [Conquest of Azeroth](#conquest-of-azeroth) — steps specific to CoA

Start with the section for your platform, it will point you to the relevant parts of the common section.


# Common

Everything in this section applies to both AzerothCore and Conquest of Azeroth.

## Obtaining the Sources

There are three ways to obtain the module sources. In all cases the module goes into the `modules` directory of your source checkout.

### Method 1: Specific release tag (recommended)

Clone a specific tagged release:

```sh
cd modules
git clone --depth 1 --branch <tag_name> git@github.com:deseven/mod-pbc.git
```

> [!NOTE]
> For tags `2026-08-10` and older (before the repo was renamed from `mod-playerbots-characters` to `mod-pbc`), append the old folder name to the clone command, i.e. `git clone --depth 1 --branch 2026-08-10 git@github.com:deseven/mod-pbc.git mod-playerbots-characters`

When you want to upgrade to a newer release tag:

```sh
cd modules/mod-pbc
git fetch --tags
git checkout <new_tag_name>
```

A list of all available tags can be found on the [Releases](https://github.com/deseven/mod-pbc/releases) page.

### Method 2: Source code archive

Download a `.zip` or `.tar.gz` source archive from the [Releases](https://github.com/deseven/mod-pbc/releases) page and extract it into the `modules` directory. The extracted folder must be named `mod-pbc` or `mod-playerbots-characters` (legacy fallback).

### Method 3: Latest (bleeding edge)

Clone the repository normally to get the latest changes:

```sh
cd modules
git clone git@github.com:deseven/mod-pbc.git
```

This gives you the most recent commits from the `main` branch. You can `git pull` at any time to update.

## Build Notes

After obtaining the sources using any of the methods above, rebuild the server normally.

> [!NOTE]
> The module bundles [nlohmann/json](https://github.com/nlohmann/json) in `deps/nlohmann/json.hpp` and [yhirose/cpp-httplib](https://github.com/yhirose/cpp-httplib) in `deps/yhirose/cpp-httplib/httplib.h`. HTTPS model connections require OpenSSL at build time.

If [mod_weather_vibe](https://github.com/hermensbas/mod_weather_vibe) is also installed, weather states from it will be included in the character's scene description.

## Configuration

Copy `conf/playerbots_characters.conf.dist` from the module to your server's module config directory as `playerbots_characters.conf` (the exact location depends on the platform, see the sections below).

### Model Connection Setup

Each LLM connection is defined in a standalone JSONC file (JSON with comments). To get started, pick one of the ready-made `.jsonc.example` templates in the [`connections/`](../connections/) directory, copy it to a new file **without** the `.example` suffix, and point the relevant config parameter at it. For the full setup guide — supported API types, config parameters, tested providers and per-task routing — see [Model Connections](CONNECTIONS.md).

After configuring, use `.chars connection-test` (optionally with a connection name like `utility`) to verify that a connection is working.

> [!IMPORTANT]
> Due to the complexity and length of the prompts, locally-run models on average home hardware will generally struggle and produce low-quality output as context grows. A cloud-based model with a large context window is recommended. Make sure to also adjust `PBC.MaxHistoryCtx` accordingly — a good starting point is around 25% of the model's total context window. Aim for at least 32k in general, anything less could lead to poor efficiency of character relationship tracking and memory extraction.

### HTTP Server & Web App

The built-in HTTP/WS server with the web app is disabled by default. To enable it, read the `HTTP SERVER` section of `playerbots_characters.conf` and follow the comments there. See the [API reference](API.md) and the [frontend readme](../frontend/README.md) for more details.

> [!NOTE]
> If you plan to use the web app for a considerable amount of time without touching the game, it's also recommended to set `PreventAFKLogout` to `2` in your `worldserver.conf`.


# AzerothCore

Steps specific to stock AzerothCore with [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots).

1. Follow the [mod-playerbots installation guide](https://github.com/mod-playerbots/mod-playerbots/wiki/Installation-Guide) to set up a working server with playerbots.
2. Obtain the module sources and rebuild the server, see [Common](#obtaining-the-sources).
3. Copy `playerbots_characters.conf.dist` to `playerbots_characters.conf` in your server's module config directory. For an AzerothCore source installation this is typically `env/dist/etc/modules/`. Then complete the [common configuration](#configuration).
4. Apply the playerbots adjustments below.

## Playerbots Adjustments

Recommended adjustments to the playerbots config (`playerbots.conf`), to make bots less talkative in order to not interfere with the new character logic:

| Setting | Value | Purpose |
|---|---|---|
| `AiPlayerbot.EnableBroadcasts` | 0 | Disables loot / quest / kill broadcasts |
| `AiPlayerbot.RandomBotTalk` | 0 | Disables random talking in say / yell / general channels |
| `AiPlayerbot.RandomBotEmote` | 0 | Disables random emoting |
| `AiPlayerbot.RandomBotSuggestDungeons` | 0 | Disables dungeon suggestions in chat |
| `AiPlayerbot.EnableGreet` | 0 | Disables greeting when invited |
| `AiPlayerbot.GuildFeedback` | 0 | Disables guild event chatting |
| `AiPlayerbot.RandomBotSayWithoutMaster` | 0 | Disables bots talking without a master |
| `AiPlayerbot.RPWarningCooldown` | 999999 | Increases delay between "missing reagents" messages and such |
| `AiPlayerbot.EnableAutoTradeOnItemMention` | 0 | Disables automatic trades and item listings on item mention (from [mod-playerbots@8caf37a](https://github.com/mod-playerbots/mod-playerbots/commit/8caf37af97b74545f8fa65172095803466ad8a06)) |


# Conquest of Azeroth

`mod-pbc` supports [Conquest of Azeroth](https://github.com/Conquest-of-AzerothCore/azerothcore-wotlk-coa) with CoA Companions or SQUID Playerbots. CoA Custom Races is optional, and class names in character context follow the server's active DBC locale.

1. Add a version of `mod-pbc` that includes CoA support under `modules/mod-pbc` in a working CoA source checkout (see [Common](#obtaining-the-sources)). Build `worldserver` with `mod-pbc` and your bot provider as static modules (`-DMODULES=static`).
2. Install the resulting `worldserver` and copy `conf/playerbots_characters.conf.dist` from the module to the server's module config directory as `playerbots_characters.conf`. In a CoA Windows repack this directory is `Core/configs/modules/`. For CoA Companions, set `CoaBots.Enable = 1` in `mod_coa_playerbots.conf`. For SQUID Playerbots, set `AiPlayerbot.Enabled = 1` in `playerbots.conf`.
3. Let the AzerothCore updater apply the module's character database scripts at startup. Keep character database updates enabled in `Updates.EnableDatabases`. If you moved the source checkout after building, set `SourceDirectory` in `worldserver.conf` to its current path. For manual updates, initialize a new installation with `data/sql/characters/base/init.sql`, then apply the unapplied scripts under `data/sql/characters/updates/` in filename order to the characters database.
4. Set `PBC.Enable = 1` in `playerbots_characters.conf` and point `PBC.DefaultConnection` to your [model connection file](CONNECTIONS.md). If the server runs from a separate directory, use absolute paths for the connection file, `PBC.PromptsPath`, and `PBC.CharacterCardsPath`.
5. Start the server and run `.chars connection-test` with a GM account. Summon a companion with your bot provider and whisper it to try dialogue. Run `.chars context <companion name>` to inspect its identity and current context. You can also [opt in to combat-role text](PROMPTS.md#coa-combat-role) in a custom prompt.
