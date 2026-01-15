#ifndef GEMINI_H
#define GEMINI_H

#include "config.h"

// Initialize Gemini client (set config)
void gemini_init(BotConfig *config);

// Send a message to Gemini (with history from channel_id) and get a response
// Returns a malloc'd string that must be freed by caller
char* gemini_chat(const char *channel_id, const char *user_message, const char *username);

// Clear history for a channel
void gemini_clear_history(const char *channel_id);

#endif // GEMINI_H
