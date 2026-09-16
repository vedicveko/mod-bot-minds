#ifndef MOD_BOT_MINDS_ACTION_H
#define MOD_BOT_MINDS_ACTION_H

#include <cstdint>
#include <string>
#include <vector>

class Player;

// --------------------------------------------
// Actions: the point where a bot stops talking and does something.
//
// Two halves. BuildActionMenu works out what this bot can genuinely do for this
// person right now, and that menu goes into the prompt so the bot can only offer
// real things. Whatever the model picks is then validated back against the same
// menu, so it cannot invent a spell, a target or an amount.
//
// Execution never happens on the thread that talked to the API. Actions are
// queued and run from the world tick, which is the only place it is safe to cast
// spells, open trade windows or send mail.
// --------------------------------------------

enum class ActionKind : uint8_t
{
    None = 0,
    Buff,
    Heal,
    Resurrect,
    GiveGold,
    Follow,
    Stay,
    Emote        // queued like the rest, because emoting goes through the session
};

// What a bot may do for one person on one turn.
struct ActionMenu
{
    std::vector<std::string> buffs;          // castable and the target does not have it
    std::vector<std::string> refreshable;    // castable, but already up; only on request
    std::vector<std::string> heals;
    std::vector<std::string> alreadyHave;    // human-readable "X, 12 minutes left"
    uint32_t                 maxCopper = 0;  // 0 means no money is on offer
    bool                     giftByMail = false;
    bool                     canTakeOrders = false;
    std::string              goldRefusal;    // why not, in plain words, for the prompt

    bool Empty() const
    {
        return buffs.empty() && refreshable.empty() && heals.empty()
            && maxCopper == 0 && !canTakeOrders;
    }
};

// Something the bot wants to remember, held back until the action it describes
// has actually happened. A bot that says "sure, here's a heal" and then fails
// should not be left remembering a heal it never cast.
struct PendingMemory
{
    std::string kind;
    std::string text;
    float       salience = 0.5f;
};

// The most useful missing buff one bot could give a passerby. Score is only for
// comparing choices in the same scan; 0 means every available spell was
// inappropriate for the target's class or current situation.
struct PasserbyBuffChoice
{
    std::string spellName;
    uint32_t score = 0;
};

// A decided action, carrying only values so it can cross a thread boundary.
struct BotAction
{
    ActionKind  kind = ActionKind::None;
    uint64_t    botGuid = 0;
    uint64_t    targetGuid = 0;
    std::string spellName;         // Buff / Heal / Resurrect
    std::string command;           // Follow / Stay
    uint32_t    emoteId = 0;       // Emote
    uint32_t    copper = 0;        // GiveGold
    bool        viaMail = false;   // GiveGold delivery
    bool        promised = false;  // the bot said it would; worth apologising if it cannot
    bool        wholeGroup = false; // an order to the party, not to one bot
    bool        tradeStarted = false;  // gold: the window is open, the coin still needs putting in
    bool        goldPlaced = false;    // gold: the coin is in, the bot still needs to accept
    bool        mentionedPost = false; // the spoken line already told them to check their mail
    uint8_t     attempt = 0;
    uint32_t    readyInMs = 0;     // retry backoff

    // Committed only once the action succeeds, and dropped if it never does.
    std::vector<PendingMemory> memories;
    bool                       otherIsBot = false;
    bool                       hasRelationshipChange = false;
    float                      affinityChange = 0.0f;
    std::string                affinityReason;
};

// Map the tool's kind string onto the enum. Unknown names become None.
ActionKind ActionKindFromName(const std::string& name);

// Resolve an emote name the model chose ("wave", "laugh") to its text emote id,
// or 0 if it is not one we allow. Also returns 0 while the bot is inside its
// emote cooldown, so restraint does not depend on the model showing any.
uint32_t ResolveEmote(uint64_t botGuid, const std::string& name);

// What this bot can do for `other` right now. Empty when there is nothing to offer.
// `unprompted` means the bot is considering volunteering rather than answering,
// which is the only case where it should hold back a heal from someone at full
// health: if you ask for one, you get one.
ActionMenu BuildActionMenu(Player* bot, Player* other, bool unprompted = false);

// Rank a bot's available buffs for an unasked passerby cast. This keeps emergency
// and situational spells out of casual use, chooses class-appropriate blessings,
// and does not replace an existing paladin blessing.
PasserbyBuffChoice ChoosePasserbyBuff(Player* bot, Player* other, const ActionMenu& menu);

// Pick a spell the bot can cast right now to help a nearby stranger recover.
// Heals require a living, hurt, out-of-combat target; resurrection requires a
// dead target without an offer already waiting. Empty means this bot cannot help.
std::string ChooseRecoverySpell(Player* bot, Player* other, bool resurrect);

// The menu rendered for the prompt. Empty string when the menu is empty.
std::string DescribeActionMenu(const ActionMenu& menu, const std::string& otherName);

// Check a model-chosen action against the menu it was offered, clamping the
// amount and rejecting anything that was not on it. Returns false to drop it.
bool ValidateAction(const ActionMenu& menu, BotAction& action);

// Safe from any thread. Runs on the next world tick.
void SubmitBotAction(const BotAction& action);

// Queue an embodiment-only emote. This is independent of Actions.Enable: player
// emote reactions are presentation, not an LLM-selected gameplay action.
void SubmitBotEmote(uint64_t botGuid, uint64_t targetGuid, uint32_t emoteId);

// Drained from BotMindsConfigWorldScript::OnUpdate, on the world thread.
void RunPendingActions(uint32_t diff);

// Hold a bot still and face somebody from the time it accepts a local turn through
// the quiet period after delivery. Safe from any thread.
void HoldStillForConversation(uint64_t botGuid, uint64_t targetGuid);

// Keeps held bots planted. Also from the world tick.
void RunConversationHolds(uint32_t diff);

// Counters for `.botminds status`.
uint32_t ActionsPerformed();
uint32_t ActionsFailed();

#endif // MOD_BOT_MINDS_ACTION_H
