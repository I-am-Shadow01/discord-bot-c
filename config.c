#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"

int config_load(const char *filename, BotConfig *config) {
    FILE *f = fopen(filename, "rb");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *data = (char *)malloc(length + 1);
    fread(data, 1, length, f);
    fclose(f);
    data[length] = '\0';

    cJSON *json = cJSON_Parse(data);
    free(data);

    if (!json) return 0;

    cJSON *token = cJSON_GetObjectItemCaseSensitive(json, "token");
    cJSON *app_id = cJSON_GetObjectItemCaseSensitive(json, "application_id");
    cJSON *apis = cJSON_GetObjectItemCaseSensitive(json, "apis");
    
    config->token = token && token->valuestring ? _strdup(token->valuestring) : NULL;
    config->application_id = app_id && app_id->valuestring ? _strdup(app_id->valuestring) : NULL;
    
    config->gemini_api_key = NULL;
    config->model = NULL;
    config->system_prompt = NULL;

    if (apis) {
        cJSON *gemini = cJSON_GetObjectItemCaseSensitive(apis, "gemini");
        if (gemini) {
            cJSON *key = cJSON_GetObjectItemCaseSensitive(gemini, "api_key");
            cJSON *model = cJSON_GetObjectItemCaseSensitive(gemini, "model");
            
            if (key && key->valuestring) config->gemini_api_key = _strdup(key->valuestring);
            if (model && model->valuestring) config->model = _strdup(model->valuestring);
        }
    }

    cJSON *chat = cJSON_GetObjectItemCaseSensitive(json, "chat");
    if (chat) {
        cJSON *prompt = cJSON_GetObjectItemCaseSensitive(chat, "character_prompt");
        if (prompt && prompt->valuestring) config->system_prompt = _strdup(prompt->valuestring);
    }

    cJSON_Delete(json);
    return 1;
}

void config_cleanup(BotConfig *config) {
    if (config->token) free(config->token);
    if (config->application_id) free(config->application_id);
    if (config->gemini_api_key) free(config->gemini_api_key);
    if (config->model) free(config->model);
    if (config->system_prompt) free(config->system_prompt);
}
