#ifndef MOD_BOT_MINDS_KINDNESS_H
#define MOD_BOT_MINDS_KINDNESS_H

#include "ScriptMgr.h"

class Player;
class Spell;
class Unit;

// Nearby healer bots occasionally help real players recover without involving
// the provider. This has its own timer so a quick resurrection check does not
// increase ambient chatter or passerby-buff frequency.
class BotMindsKindnessWorldScript : public WorldScript
{
public:
    BotMindsKindnessWorldScript();
    void OnUpdate(uint32 diff) override;
};

// Correlates direct buffs and completed player-cast resurrections with the bot
// that received them.
class BotMindsReciprocityPlayerScript : public PlayerScript
{
public:
    BotMindsReciprocityPlayerScript() : PlayerScript("BotMindsReciprocityPlayerScript", {
        PLAYERHOOK_ON_SPELL_CAST,
        PLAYERHOOK_ON_PLAYER_RESURRECT,
    }) {}

    void OnPlayerSpellCast(Player* player, Spell* spell, bool skipCheck) override;
    void OnPlayerResurrect(Player* player, float restorePercent, bool& applySickness) override;
};

// Healing is credited from the final effective gain, so an overheal does not
// make a bot grateful for help it did not actually receive.
class BotMindsReciprocityUnitScript : public UnitScript
{
public:
    BotMindsReciprocityUnitScript() : UnitScript("BotMindsReciprocityUnitScript", true, {
        UNITHOOK_ON_HEAL,
    }) {}

    void OnHeal(Unit* healer, Unit* receiver, uint32& gain) override;
};

#endif // MOD_BOT_MINDS_KINDNESS_H
