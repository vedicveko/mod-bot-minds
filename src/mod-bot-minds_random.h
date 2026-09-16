#ifndef MOD_BOT_MINDS_RANDOM_H
#define MOD_BOT_MINDS_RANDOM_H

#include "ScriptMgr.h"

// --------------------------------------------
// Unprompted life around real players: coordinated ambient chatter plus occasional
// direct, capability-checked passerby buffs.
// --------------------------------------------
class BotMindsAmbientChatter : public WorldScript
{
public:
    BotMindsAmbientChatter();
    void OnUpdate(uint32 diff) override;
};

#endif // MOD_BOT_MINDS_RANDOM_H
