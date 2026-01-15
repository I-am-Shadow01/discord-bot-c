#ifndef CONFIG_H
#define CONFIG_H

#include "cJSON.h"

typedef struct {
    char *token;
    char *application_id;
    char *gemini_api_key;
    char *model;
    char *system_prompt;
} BotConfig;

// Load config from a file
int config_load(const char *filename, BotConfig *config);

// Free config resources
void config_cleanup(BotConfig *config);

#endif // CONFIG_H
