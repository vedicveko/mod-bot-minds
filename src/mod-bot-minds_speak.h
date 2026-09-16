#ifndef MOD_BOT_MINDS_SPEAK_H
#define MOD_BOT_MINDS_SPEAK_H

#include "mod-bot-minds_prompt.h"

// --------------------------------------------
// The one path from "a bot has something to say" to words in chat.
//
// Direct replies, interjections, ambient chatter and event reactions all come
// through here: same gate, same prompt builder, same provider, same memory
// write-back. There is deliberately no second route.
// --------------------------------------------

// `priority` bypasses conversational pacing for a turn addressed to this bot.
// Whether the model is allowed to remain silent is carried separately by
// TurnRequest::replyRequired. Returns true if a call was submitted.
bool RequestBotTurn(TurnRequest& request, bool priority);

#endif // MOD_BOT_MINDS_SPEAK_H
