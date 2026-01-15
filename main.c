#include <stdio.h>
#include <stdlib.h>
#include "discord.h"
#include "config.h"
#include "gemini.h"

int main(int argc, char *argv[]) {
    printf("Starting Discord Bot in C...\n");

    BotConfig config = {0};
    if (!config_load("config.json", &config)) {
        printf("Error: Could not load config.json\n");
        printf("Make sure config.json exists and has valid JSON.\n");
        return 1;
    }

    if (!config.token) {
        printf("Error: Token not found in config.json.\n");
        return 1;
    }

    DiscordBot bot;
    discord_init(&bot, &config);
    gemini_init(&config);

    // Always call register, it will fetch ID if needed
    discord_register_commands(&bot);

    if (discord_connect(&bot)) {
        discord_run(&bot);
    } else {
        printf("Failed to connect to Discord Gateway.\n");
    }

    discord_cleanup(&bot);
    config_cleanup(&config);
    return 0;
}
