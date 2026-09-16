# mod-bot-minds

> [!IMPORTANT]
> **This is a vibe-coded community fork.** Most of the additions in this fork were developed collaboratively with
> [OpenAI Codex](https://openai.com/codex/) and tested on a private AzerothCore server. It is public in the hope that
> it is useful to somebody else and saves them a few hours reproducing the same work with an AI coding agent. Expect
> rough edges, review the code and configuration for your own server, and test it before relying on it.

> [!NOTE]
> This repository is a fork of [Tom Glenn's mod-bot-minds](https://github.com/tomglenn/mod-bot-minds). The original
> Git history and AGPL-3.0 license are preserved, and Tom Glenn remains the author of the project this work builds on.

Playerbots that talk like people. Each bot has a persona, remembers what happened to it, keeps track of how it feels about you, and follows the thread of a conversation instead of shouting over it.

> [!IMPORTANT]
> **This only works on AzerothCore with Playerbots.** It needs [liyunfan1223/azerothcore-wotlk](https://github.com/liyunfan1223/azerothcore-wotlk) with [mod-playerbots](https://github.com/liyunfan1223/mod-playerbots) enabled, and it calls into the Playerbots API directly. It will not build against upstream AzerothCore, because without bots there is nobody for it to give a mind to.

> [!NOTE]
> **Heavily inspired by [mod-ollama-chat](https://github.com/DustinHendrickson/mod-ollama-chat) by Dustin Hendrickson**, which is where this started and which deserves the credit for the idea of giving playerbots an LLM voice. The chat pipeline here has since been rewritten around personas, persistent memory and a single shared prompt path; the HTTP client, config plumbing and event hook surface still trace back to that project. Bot Minds can now use the native Ollama chat API locally or through Ollama Cloud.

> [!CAUTION]
> **Hosted models cost money.** Every line a bot speaks is an API call. A local Ollama server has no per-call provider charge; hosted providers do. See [Cost](#cost) for the arithmetic and the settings that cap it.

## What it does

**Talk to a bot and the right one answers.** Say a bot's name or select it and that bot replies. Ask the room for a buff, heal or other concrete help and Bot Minds prefers a nearby character that can really provide it. Follow up without naming anyone and the answer comes from the bot you were already talking to, not from three strangers who happened to overhear. Brief closers can end naturally instead of forcing one more exchange.

**They sound like players, not heroes.** Bots chat the way people in a game chat: short, casual, off the cuff. No monologues about destiny, no narrating their own actions, no sliding into stagey roleplay halfway through a conversation.

**They remember.** Tell a bot you're working on Brotherhood of Thieves, ask about it an hour later or after a server restart, and it knows. Memories are saved the moment they happen, so nothing is lost when the server goes down. `.botminds memory <bot>` shows you what a bot is carrying.

**Every bot is a different person.** Each one has a stable mix of temperament, gameplay interests and subtle chat
habits derived from its identity. Recent wins, deaths, loot and kindness can color that voice for a while without
rewriting the underlying personality. `.botminds persona <bot>` shows both the permanent profile and any active mood.

**They know what they are doing.** A bot's prompt carries its own state: health, mana, spec, money, group-mates by name, the quests it is part way through, whether it is resting or mounted. Ask what it is up to and you get the truth rather than an invention.

**They remember what matters.** A memory's worth is its importance discounted by how stale it is, so being pulled out of a bad fight outlives a month of small talk, while two equally dull lines are separated by which happened lately. Recalling something keeps it fresh, and once a bot is holding too much the dullest is folded into a single hazy summary rather than forgotten outright.

**They stop to talk.** As soon as a bot accepts your local message it plants itself and turns to face you while the reply is generated and typed. The hold refreshes when it speaks, so you are not chasing it out of earshot mid-conversation. Combat interrupts it immediately, and bots in your group are left alone since they are following you already.

**They form opinions of you.** Help a bot out and it warms to you. Be rude and it cools off, and it remembers why. `.botminds feelings <bot>` shows where you stand.

**They do things, not just say them.** Ask a priest for a buff and you get Power Word: Fortitude, not a promise. Ask a bot who likes you for a few coppers and it opens a trade, or puts it in the post if you are not grouped. Ask one of your own party bots to come with you, in whatever words you like, and it comes. A bot is told what it can genuinely do before it answers, so it never offers something it cannot deliver, and it turns you down in its own voice when the answer is no.

**They talk when you don't.** Bots grumble about full bags, mention what's around them, and react when someone dies, levels up, earns an achievement or wins a duel. Now and then a nearby capable bot quietly gives you a genuinely missing buff as you pass, without spending an LLM request. It favors useful long buffs for your class, respects existing blessings, and saves aquatic or emergency spells for situations where they make sense. Friendly healers may also patch up a badly hurt player after combat or offer a resurrection, without interfering in battlegrounds or duplicating an offer already waiting. Ambient timing is coordinated per audible scene rather than per bot, so a crowd supplies more possible speakers without becoming a wall of chatter. Lines can stay in local Say and party chat or use configured General, Trade, Looking For Group and Guild Recruitment channels; a real player must be able to hear the chosen destination before a call is made.

**They notice kindness.** Heal, buff or resurrect a bot and it remembers who helped. Once the danger has passed it thanks you with an ordinary in-game emote and, sometimes, one of several short local lines without immediately repeating the same one. Healing-over-time ticks share a cooldown and group buffs produce at most one visible response at a time, so gratitude does not become spam. This path is deterministic and makes no provider call.

**They react when you emote at them.** Target a bot with a text emote and it may mirror you, answer with a fitting counter-emote, or occasionally reply in its own voice. Gesture-only reactions do not call the provider, and a per-player/bot cooldown prevents emote spam.

**They share the journey.** A party coordinates one reaction when it enters a zone, reaches town, steps into a dungeon or defeats a boss. Meaningful successes become shared memories and gently shape relationships between the bots that were there; merely crossing a zone line does not. A travel cooldown prevents one teleport from producing separate map, zone and town remarks.

**They recognize old friends.** Relationship timestamps survive restarts. When somebody rejoins after a long absence, one bot that genuinely knows them may wave and greet them with access to the pair's real memories. Known groupmates may also say a brief goodbye when someone leaves; strangers are left alone.

**Their words match their plans.** The prompt includes Playerbots' real travel target, a plain description of its latest visible activity, detailed progress for active quest objectives, and nearby useful NPCs such as innkeepers, repairers and flight masters. Grouped bots explicitly know they are following the party rather than pursuing their solo travel plan.

**Memories can be resolved.** Memories that name an active quest are tagged as pending using the schema's existing lifecycle fields. Completing that quest retires stale intentions from active recall while preserving a separate shared-adventure memory of finishing it. `.botminds memory <bot>` displays pending and done states.

## Requirements

- AzerothCore (liyunfan1223 fork) with mod-playerbots.
- An Anthropic or OpenAI API key, an Ollama Cloud API key, or a reachable local Ollama server. See [Providers](#providers).
- fmt (comes with AzerothCore), plus OpenSSL when using an HTTPS provider. nlohmann/json and cpp-httplib are bundled in this repo.

## Installing

```bash
cd /path/to/azerothcore/modules
git clone https://github.com/tomglenn/mod-bot-minds.git
```

Rebuild the worldserver:

```bash
cd /path/to/azerothcore/build
cmake ..
make -j$(nproc)
```

Copy the config template and edit it:

```bash
cp /path/to/azerothcore/modules/mod-bot-minds/conf/mod_bot_minds.conf.dist \
   /path/to/azerothcore/env/dist/etc/modules/mod_bot_minds.conf
```

Set the provider, model and credentials in that copy. Everything else has a working default. Hosted keys can be read from an environment variable instead of being stored in the file. A local Ollama endpoint needs no key.

Do not enable Bot Minds and mod-ollama-chat together. Their script names and playerbot event hooks overlap, so they may not link cleanly and would compete to answer the same messages. Treat Bot Minds as the replacement module.

Start the server. The module's SQL creates its tables on import.

> [!TIP]
> Keep your real `mod_bot_minds.conf` out of version control. This repo's `.gitignore` already excludes `conf/*.conf` while keeping the `.dist` template, so the file you edit will not be committed by accident.

Consider giving playerbots a command prefix too, in `playerbots.conf`:

```
AiPlayerbot.CommandPrefix = "!"
```

Without one, playerbots prefix-matches ordinary speech against its command names, so
"do you have any spare silver?" is read as the `do` command and "follow me" fires `follow` with
"me" discarded. With a prefix it only acts on `!follow`, `!stay` and so on, leaving plain English
to this module. The module reads that setting: when a prefix is configured, anything unprefixed
is treated as conversation, including a bare "follow", which it will turn into the follow action
for a bot you have grouped with.

Finally, turn off playerbots' own canned chatter so it does not talk over the module. In `playerbots.conf`:

```
AiPlayerbot.EnableBroadcasts = 0
AiPlayerbot.RandomBotTalk = 0
AiPlayerbot.RandomBotEmote = 0
AiPlayerbot.RandomBotSuggestDungeons = 0
AiPlayerbot.EnableGreet = 0
AiPlayerbot.GuildFeedback = 0
AiPlayerbot.RandomBotSayWithoutMaster = 0
```

## Providers

**Anthropic is the tested path.** Everything in this repo was built and run against Claude Haiku 4.5, and that is what the defaults point at.

**The OpenAI provider is untested.** It is wired up, it compiles, it sends the `bot_turn` tool to `/v1/chat/completions` and parses tool calls back out, and as far as the documented API goes the shape is right. But no call has ever been made through it. It sends `max_completion_tokens` and falls back to `max_tokens` if the API rejects that, and any non-200 response is logged with the API's own error text, so if bots go quiet the server log should tell you why. Treat a failure there as a bug worth reporting rather than a wall.

**Ollama uses its native `/api/chat` tool-calling API.** The selected model must support tools. Local Ollama needs no API key:

```ini
BotMinds.Provider = "ollama"
BotMinds.Model = "qwen3:8b"
BotMinds.Url = "http://localhost:11434/api/chat"
```

Direct Ollama Cloud uses the same request and response shape with Bearer authentication:

```ini
BotMinds.Provider = "ollama"
BotMinds.Model = "gemma4:cloud"
BotMinds.Url = "https://ollama.com/api/chat"
BotMinds.ApiKeyEnv = "OLLAMA_API_KEY"
```

The server process must inherit the named environment variable. You can put the key directly in `BotMinds.ApiKey`, but the environment-variable form avoids keeping a secret in the module config. Use `.botminds test hello` after startup to exercise the configured provider without waiting for a chat event.

The Ollama path deliberately uses native tool calls rather than the `format` structured-output option. That keeps the same `bot_turn` contract used by the other providers and works with direct Ollama Cloud, where structured outputs are not available.

If you get it working, or find it broken, an issue or a PR would be welcome.

### Moving from mod-ollama-chat

Bot Minds is a replacement, not an in-place upgrade. Disable mod-ollama-chat, install Bot Minds, and translate the small provider block rather than copying the old config wholesale:

| mod-ollama-chat | Bot Minds |
| --- | --- |
| `OllamaChat.Url` using `/api/generate` | `BotMinds.Url` using `/api/chat` |
| `OllamaChat.Model` | `BotMinds.Model` |
| `OLLAMA_API_KEY` auto-detection | `BotMinds.ApiKeyEnv = "OLLAMA_API_KEY"` |
| `OllamaChat.MaxConcurrentQueries` | `BotMinds.Limits.MaxConcurrentCalls` plus `BotMinds.WorkerThreads` |
| reply and chatter chances | the `BotMinds.ReplyChance.*`, `Ambient.*` and `Events.*` settings |
| random General/Trade/LFG/Guild Recruitment chatter | `BotMinds.Ambient.Channel.*` |
| response cleanup and repetition limits | `BotMinds.Response.*`, `Limits.PerScope*` and `Repetition.*` |

The old prompt templates, sentiment database and RAG index are not imported. Bot Minds replaces them with its persona, transcript, relationship, live world-state and persistent-memory pipeline. It starts with fresh Bot Minds tables; validate the new behavior before considering a one-off data migration.

## Configuration

`conf/mod_bot_minds.conf.dist` is the template, with every setting annotated and set to its default. There is more in there than is listed below (memory sizing, transcript length, per-event reaction chances, the remaining reply chances and channel routing toggles), but these are the dials most worth reaching for:

| Setting | Default | What it does |
| --- | --- | --- |
| `BotMinds.Enable` | 1 | Master switch for the module. |
| `BotMinds.ApiKey` | empty | Hosted-provider API key. Optional for local Ollama and when `ApiKeyEnv` is set. |
| `BotMinds.ApiKeyEnv` | empty | Environment variable that contains the API key; use `OLLAMA_API_KEY` for Ollama Cloud. |
| `BotMinds.Provider` | `anthropic` | `anthropic`, `openai` or `ollama`. |
| `BotMinds.Model` | `claude-haiku-4-5` | Any tool-calling model from that provider. |
| `BotMinds.Url` | local Ollama chat URL | Native Ollama chat endpoint; ignored by Anthropic and OpenAI. |
| `BotMinds.WorkerThreads` | 3 | Managed HTTP workers. A restart is required after changing it. |
| `BotMinds.MaxQueueDepth` | 64 | Most provider requests allowed to wait for a worker; 0 is unlimited. |
| `BotMinds.MaxReplyChars` | 200 | Hard limit on a spoken line. The model is told this number. |
| `BotMinds.SayDistance` | 30.0 | Earshot in yards. Governs everything spoken out loud, including how near you must be for a bot to speak unprompted, and how far bots look around for something to remark on. |
| `BotMinds.Route.HandleChannel` | 0 | General, Trade and the rest. Off because a busy channel burns calls. |
| `BotMinds.ReplyChance.Say` | 70 | How likely an uninvolved bystander is to pick up an open remark. Party, Guild, Channel, Whisper and BotToBot have their own. |
| `BotMinds.Attention.FloorWindowSec` | 60 | How long the bot you are talking to keeps the right to answer you. |
| `BotMinds.Attention.MaxBotsToPick` | 2 | Most bots that may answer one line. |
| `BotMinds.Limits.PerBotCooldownSec` | 12 | Quiet time between one bot's unprompted lines. A direct answer ignores it. |
| `BotMinds.Limits.PerScopeCooldownSec` | 15 | Quiet time between unprompted lines for one local listener, party, guild or numbered channel. |
| `BotMinds.Limits.MaxCallsPerMinute` | 60 | Ceiling on API calls in any one minute. The safety rail on your bill. |
| `BotMinds.Ambient.Chance` | 25 | Chance that an audible scene speaks when its timer is due; failed rolls retry every 30 seconds. |
| `BotMinds.Ambient.Channel.General` | 0 | Let eligible ambient lines use the bot's localized General channel when a real player is in it. Trade, LFG and Guild Recruitment have matching toggles. |
| `BotMinds.WorldLife.Enable` | 1 | Shared journey reactions, reunions, bot relationships and contextual idle gestures. |
| `BotMinds.WorldLife.Reunion.MinAbsenceSec` | 21600 | How long a known person must be absent before a bot treats their return as a reunion. |
| `BotMinds.WorldLife.IdleGestureChance` | 20 | Chance that a due ambient scene uses a free, contextual gesture instead of an LLM line. |
| `BotMinds.Repetition.SimilarityThreshold` | 0.72 | Drop generated lines too similar to recent bot or scope history. |
| `BotMinds.EmoteReaction.Enable` | 1 | React when a real player targets a bot with a text emote. |
| `BotMinds.Emote.CooldownSec` | 180 | Least time between one bot's gestures. 0 stops them emoting. |
| `BotMinds.Conversation.HoldStillSec` | 8 | How long a bot stops and faces you from accepting a direct local turn through the quiet period afterward. |
| `BotMinds.Actions.Gold.MaxCopper` | 5000 | Hard ceiling on a single gift, in copper. 0 stops bots giving money. |
| `BotMinds.Actions.Gold.MinAffinity` | 0.25 | How much a bot must like you before it parts with coin. |
| `BotMinds.Actions.Unprompted.Chance` | 20 | Chance every 30 seconds that a nearby capable bot gives a real player the best missing, context-appropriate buff; no LLM call is used. |
| `BotMinds.Recovery.Enable` | 1 | Allow nearby healer bots to offer resurrection or post-combat healing to real players. |
| `BotMinds.Recovery.HealBelowPct` | 55 | Health threshold for spontaneous post-combat healing. |
| `BotMinds.Reciprocity.Enable` | 1 | Thank real players for effective heals, direct buffs and completed resurrections, and credit the relationship. |
| `BotMinds.Reciprocity.AffinityGain` | 0.02 | Relationship increase for one credited act of help. |
| `BotMinds.Typing.Enable` | 0 | Hold a finished line back as though the bot were typing it. |
| `BotMinds.DebugEnabled` | 0 | Log who was picked to answer, who stayed quiet, and why. |

## Commands

All require SEC_ADMINISTRATOR and work from the server console too.

| Command | What it shows |
| --- | --- |
| `.botminds status` | Provider, model, limits, calls made since startup, and how much is stored |
| `.botminds reload` | Re-read the config and rebuild the provider |
| `.botminds test <prompt>` | Queue a provider test and write the result or error to the server log |
| `.botminds persona <bot>` | That bot's temperament, interests, chat habits, active mood and custom fields |
| `.botminds memory <bot>` | The 25 newest memories, read from the database |
| `.botminds feelings <bot>` | Affinity towards everyone it has an opinion about |
| `.botminds forget <bot>` | Delete that bot's memories and relationships |

## What bots will do for you

| Ask for | Who obliges | How |
| --- | --- | --- |
| A buff | Any bot of a class that has one, in earshot | Cast on you directly. It will not offer a buff you already have, cannot afford, or does not know. |
| A heal | Any healing class, out of combat | Only offered when you are actually hurt. |
| Gold | A bot that likes you enough, once per day each | Trade window if you are grouped and standing close, otherwise it arrives by post. |
| Follow, wait | Bots you have grouped with | Routed through playerbots' own command handler, so the permissions are exactly the same as typing the command. |

Two things are deliberately absent. Bots will not hand over items, and they will not invite you
to a group: playerbots disbands any group led by a random bot, so the invite would undo itself.

The gold cap is a design number rather than a fraction of the bot's purse. Bots are created
holding hundreds of gold, so "whatever it can spare" would hand a low level character a fortune.
The default is at most 50 silver, scaled by the giving bot's level and by how well it knows you,
once per bot per day, and the bot remembers having done it so you cannot talk it round.

Unasked recovery is deliberately conservative: it is friendly-faction only, out of combat, never
runs in battlegrounds, respects existing resurrection offers, and has a per-player cooldown. It and
social gratitude use normal game actions rather than the LLM, so neither behavior adds provider cost.

## Cost

A turn is roughly 1000 to 1400 input tokens (persona, world state, relationship, memories, recent chat, the tool schema) and 80 to 150 output. On Claude Haiku 4.5 that is about $0.002 per line a bot speaks. Ordinary play is a few calls a minute, so single-digit cents an hour. Ollama Cloud pricing depends on the selected model; a local Ollama server has no provider charge.

Calls only happen where a real player can hear the result. Say needs someone within `SayDistance` and keeps separate transcript and pacing state per local audience; party and guild chat use their membership instead; ambient numbered-channel chatter requires a real player to be joined to the exact resolved channel. Bots only answer each other where a person is watching.

`Limits.MaxCallsPerMinute` bounds the worst case no matter what happens in game. At the default of 60 that is 3600 calls an hour, or roughly $7, and you would have to work hard to get near it. `.botminds status` reports the running total, which beats guessing.

## How a line gets spoken

1. **Chat hook** turns an incoming message into a scope (say, party, guild, channel, whisper) and records it in that scope's transcript. Playerbot commands are filtered out.
2. **Attention** decides who answers: a named bot, the bot holding the floor, one member of a small group, or a chance roll per listener. This is the only place reply probability lives.
3. **Governor** decides who may: provider available, hard cap and concurrency free, per-bot and per-scope pacing, proximity. A direct answer to someone who addressed the bot skips pacing and proximity.
4. **Prompt** is assembled from persona, world state, relationship, memory and the recent transcript, plus one instruction for why this bot is speaking and the shared voice rules.
5. **Dispatcher** sends the request through a bounded worker pool, so slow model calls never block the world thread and shutdown never leaves detached requests behind.
6. **Provider** returns a `bot_turn` tool call: whether to speak, the words, new memories, relationship change.
7. **Speak** runs back on the world thread, removes reasoning tags, stage directions, markdown and decorative Unicode, truncates safely on a UTF-8 sentence or word boundary, rejects repetitions, puts the line in the right channel, records it, and writes the memories.

## Database

Three tables in the characters database, created by the module's SQL:

- `mod_bot_minds_persona`: one row per bot for its identity and optional hand-written overrides. The default
  temperament, interests and chat habits are derived from the bot's GUID, so existing rows gain improvements
  without a migration.
- `mod_bot_minds_memory`: what bots remember, with a subject and a salience.
- `mod_bot_minds_relationship`: affinity per bot per person.

## Debugging

`BotMinds.DebugEnabled = 1` logs who was picked to answer, who stayed quiet, and why. `BotMinds.DebugShowFullPrompt = 1` adds the full prompt for every line, which is very noisy but useful when the voice is wrong. `.botminds status` includes queue depth, in-flight calls, failures and the last provider error; `.botminds test <prompt>` checks the provider path directly.

## License

GNU AGPL v3, consistent with AzerothCore.
