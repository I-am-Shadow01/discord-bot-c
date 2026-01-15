#ifndef DISCORD_H
#define DISCORD_H

#include <windows.h>
#include <winhttp.h>
#include "cJSON.h"
#include "config.h"

#define DISCORD_API_BASE "https://discord.com/api/v10"
#define DISCORD_GATEWAY_URL "wss://gateway.discord.gg/?v=10&encoding=json"

typedef struct {
    char *token;
    char *application_id;
    HINTERNET hSession;
    HINTERNET hConnect;
    HINTERNET hRequest; // For REST
    HINTERNET hWebSocket; // For Gateway
    BOOL is_connected;
    int heartbeat_interval;
    int last_sequence;
} DiscordBot;

// Initialize the bot structure
void discord_init(DiscordBot *bot, BotConfig *config);

// Connect to the Gateway
BOOL discord_connect(DiscordBot *bot);

// Main loop to listen for events
void discord_run(DiscordBot *bot);

// Clean up resources
void discord_cleanup(DiscordBot *bot);

// Helper to send a message to a channel
void discord_send_message(DiscordBot *bot, const char *channel_id, const char *content);

// Register slash commands
void discord_register_commands(DiscordBot *bot);

// Send an interaction response
// response_type: 4 = immediate message, 5 = deferred (thinking)
void discord_send_interaction_response(DiscordBot *bot, const char *interaction_id, const char *interaction_token, int response_type, const char *content);

// Edit an existing interaction response (used after deferring)
void discord_edit_interaction_response(DiscordBot *bot, const char *interaction_token, const char *content);

#endif // DISCORD_H
