#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h> 
#include "discord.h"
#include "gemini.h"

#pragma comment(lib, "winhttp.lib")

// --- Internal Helper: Command Thread Context ---
typedef struct {
    DiscordBot *bot;
    char *interaction_id;
    char *interaction_token;
    char *channel_id;
    char *user_msg;
    char *username;
    int is_clear_cmd;
} CommandContext;

void free_command_context(CommandContext *ctx) {
    free(ctx->interaction_id);
    free(ctx->interaction_token);
    free(ctx->channel_id);
    if (ctx->user_msg) free(ctx->user_msg);
    if (ctx->username) free(ctx->username);
    free(ctx);
}

// --- Internal Helper: Automatic Application ID Discovery ---
static void fetch_application_id(DiscordBot *bot) {
    if (bot->application_id) return;

    printf("[INFO] Fetching Application ID from Discord...\n");
    HINTERNET hSession = WinHttpOpen(L"DiscordBotC/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET hConnect = WinHttpConnect(hSession, L"discord.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/api/v10/users/@me", NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    
    if (hRequest) {
        char auth[256];
        sprintf_s(auth, 256, "Authorization: Bot %s", bot->token);
        wchar_t w_auth[256];
        MultiByteToWideChar(CP_UTF8, 0, auth, -1, w_auth, 256);
        WinHttpAddRequestHeaders(hRequest, w_auth, -1, WINHTTP_ADDREQ_FLAG_ADD);
        
        if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0)) {
            if (WinHttpReceiveResponse(hRequest, NULL)) {
                DWORD dwSize = 0;
                char *resp = NULL;
                DWORD total = 0;
                while (WinHttpQueryDataAvailable(hRequest, &dwSize) && dwSize > 0) {
                    char *chunk = malloc(dwSize);
                    DWORD read = 0;
                    if (WinHttpReadData(hRequest, chunk, dwSize, &read)) {
                        resp = realloc(resp, total + read + 1);
                        memcpy(resp + total, chunk, read);
                        total += read;
                        resp[total] = 0;
                    }
                    free(chunk);
                }
                if (resp) {
                    cJSON *json = cJSON_Parse(resp);
                    if (json) {
                        cJSON *id = cJSON_GetObjectItem(json, "id");
                        if (id) bot->application_id = _strdup(id->valuestring);
                        cJSON_Delete(json);
                    }
                    free(resp);
                }
            }
        }
        WinHttpCloseHandle(hRequest);
    }
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
}

// --- Background Command Processor ---
unsigned __stdcall command_thread_proc(void *lpParam) {
    CommandContext *ctx = (CommandContext*)lpParam;
    
    if (ctx->is_clear_cmd) {
        gemini_clear_history(ctx->channel_id);
        discord_send_interaction_response(ctx->bot, ctx->interaction_id, ctx->interaction_token, 4, "🧹 ประวัติสนทนาในห้องนี้ถูกล้างเรียบร้อยแล้วค่ะ!");
    } else {
        discord_send_interaction_response(ctx->bot, ctx->interaction_id, ctx->interaction_token, 5, NULL);
        char *reply = gemini_chat(ctx->channel_id, ctx->user_msg, ctx->username);
        discord_edit_interaction_response(ctx->bot, ctx->interaction_token, reply);
        free(reply);
    }

    free_command_context(ctx);
    return 0;
}

// --- Public Interface ---

void discord_init(DiscordBot *bot, BotConfig *config) {
    memset(bot, 0, sizeof(DiscordBot));
    bot->token = _strdup(config->token);
    bot->application_id = config->application_id ? _strdup(config->application_id) : NULL;
    bot->hSession = WinHttpOpen(L"DiscordBotC/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
}

BOOL discord_connect(DiscordBot *bot) {
    if (!bot->hSession) return FALSE;

    bot->hConnect = WinHttpConnect(bot->hSession, L"gateway.discord.gg", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!bot->hConnect) return FALSE;

    bot->hRequest = WinHttpOpenRequest(bot->hConnect, L"GET", L"/?v=10&encoding=json", NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!bot->hRequest) return FALSE;

    WinHttpSetOption(bot->hRequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0);
    if (!WinHttpSendRequest(bot->hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0)) return FALSE;
    if (!WinHttpReceiveResponse(bot->hRequest, NULL)) return FALSE;

    bot->hWebSocket = WinHttpWebSocketCompleteUpgrade(bot->hRequest, 0);
    WinHttpCloseHandle(bot->hRequest);
    bot->hRequest = NULL;

    if (!bot->hWebSocket) return FALSE;
    bot->is_connected = TRUE;
    printf("[INFO] Connected to Discord Gateway!\n");

    // Identify Identity
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddNumberToObject(payload, "op", 2);
    cJSON *d = cJSON_CreateObject();
    cJSON_AddItemToObject(payload, "d", d);
    cJSON_AddStringToObject(d, "token", bot->token);
    cJSON_AddNumberToObject(d, "intents", 513); // GUILDS | GUILD_MESSAGES
    
    cJSON *props = cJSON_CreateObject();
    cJSON_AddItemToObject(d, "properties", props);
    cJSON_AddStringToObject(props, "os", "windows");
    cJSON_AddStringToObject(props, "browser", "discord-bot-c");
    cJSON_AddStringToObject(props, "device", "discord-bot-c");

    char *json = cJSON_PrintUnformatted(payload);
    WinHttpWebSocketSend(bot->hWebSocket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, (PVOID)json, (DWORD)strlen(json));
    
    free(json);
    cJSON_Delete(payload);
    return TRUE;
}

DWORD WINAPI discord_heartbeat_thread(LPVOID lpParam) {
    DiscordBot *bot = (DiscordBot*)lpParam;
    while (bot->is_connected) {
        Sleep(bot->heartbeat_interval);
        cJSON *pb = cJSON_CreateObject();
        cJSON_AddNumberToObject(pb, "op", 1);
        cJSON_AddNullToObject(pb, "d");
        char *js = cJSON_PrintUnformatted(pb);
        WinHttpWebSocketSend(bot->hWebSocket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, (PVOID)js, (DWORD)strlen(js));
        free(js);
        cJSON_Delete(pb);
    }
    return 0;
}

void discord_run(DiscordBot *bot) {
    if (!bot->is_connected) return;

    BYTE buffer[65536];
    DWORD read;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE type;

    char *json_acc = malloc(65536);
    size_t acc_size = 65536;
    size_t acc_len = 0;

    printf("[INFO] Bot is now running and listening for events...\n");
    while (bot->is_connected) {
        if (WinHttpWebSocketReceive(bot->hWebSocket, buffer, sizeof(buffer) - 1, &read, &type) != ERROR_SUCCESS) break;

        if (acc_len + read >= acc_size) {
            acc_size *= 2;
            json_acc = realloc(json_acc, acc_size);
        }
        memcpy(json_acc + acc_len, buffer, read);
        acc_len += read;
        json_acc[acc_len] = 0;

        if (type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE) continue;

        cJSON *json = cJSON_Parse(json_acc);
        acc_len = 0; // Reset for next msg
        if (!json) continue;

        cJSON *op = cJSON_GetObjectItem(json, "op");
        cJSON *t = cJSON_GetObjectItem(json, "t");
        cJSON *d = cJSON_GetObjectItem(json, "d");

        if (op && op->valueint == 10) { // HELLO
            cJSON *intv = cJSON_GetObjectItem(d, "heartbeat_interval");
            if (intv) {
                bot->heartbeat_interval = intv->valueint;
                CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)discord_heartbeat_thread, bot, 0, NULL);
            }
        }

        if (t && t->valuestring && strcmp(t->valuestring, "INTERACTION_CREATE") == 0) {
            cJSON *id = cJSON_GetObjectItem(d, "id");
            cJSON *token = cJSON_GetObjectItem(d, "token");
            cJSON *data = cJSON_GetObjectItem(d, "data");
            cJSON *name = cJSON_GetObjectItem(data, "name");
            cJSON *chan = cJSON_GetObjectItem(d, "channel_id");
            
            if (name && (strcmp(name->valuestring, "chat") == 0 || strcmp(name->valuestring, "clear") == 0)) {
                CommandContext *ctx = calloc(1, sizeof(CommandContext));
                ctx->bot = bot;
                ctx->interaction_id = _strdup(id->valuestring);
                ctx->interaction_token = _strdup(token->valuestring);
                ctx->channel_id = _strdup(chan->valuestring);
                ctx->is_clear_cmd = (strcmp(name->valuestring, "clear") == 0);
                
                if (!ctx->is_clear_cmd) {
                    cJSON *opts = cJSON_GetObjectItem(data, "options");
                    if (opts && cJSON_GetArraySize(opts) > 0) {
                        cJSON *v = cJSON_GetObjectItem(cJSON_GetArrayItem(opts, 0), "value");
                        if (v) ctx->user_msg = _strdup(v->valuestring);
                    }
                    if (!ctx->user_msg) ctx->user_msg = _strdup("Hello");
                    
                    cJSON *mbr = cJSON_GetObjectItem(d, "member");
                    cJSON *usr = mbr ? cJSON_GetObjectItem(mbr, "user") : cJSON_GetObjectItem(d, "user");
                    if (usr) {
                        cJSON *un = cJSON_GetObjectItem(usr, "username");
                        if (un) ctx->username = _strdup(un->valuestring);
                    }
                }
                _beginthreadex(NULL, 0, command_thread_proc, ctx, 0, NULL);
            }
        }

        if (t && t->valuestring && strcmp(t->valuestring, "MESSAGE_CREATE") == 0) {
            cJSON *auth = cJSON_GetObjectItem(d, "author");
            cJSON *bot_flag = cJSON_GetObjectItem(auth, "bot");
            if (!bot_flag || !cJSON_IsTrue(bot_flag)) {
                cJSON *c = cJSON_GetObjectItem(d, "content");
                if (c && strncmp(c->valuestring, "!ping", 5) == 0) {
                    discord_send_message(bot, cJSON_GetObjectItem(d, "channel_id")->valuestring, "Pong! 🏓");
                }
            }
        }
        cJSON_Delete(json);
    }
    free(json_acc);
}

void discord_register_commands(DiscordBot *bot) {
    fetch_application_id(bot);
    if (!bot->application_id) return;

    printf("[INFO] Syncing slash commands for App ID: %s\n", bot->application_id);
    HINTERNET hSession = WinHttpOpen(L"DiscordBotC/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET hConnect = WinHttpConnect(hSession, L"discord.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    wchar_t path[256];
    swprintf_s(path, 256, L"/api/v10/applications/%S/commands", bot->application_id);
    
    char auth[256];
    sprintf_s(auth, 256, "Authorization: Bot %s\r\nContent-Type: application/json", bot->token);
    wchar_t w_auth[256];
    MultiByteToWideChar(CP_UTF8, 0, auth, -1, w_auth, 256);

    // Register /chat
    HINTERNET hReq = WinHttpOpenRequest(hConnect, L"POST", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (hReq) {
        WinHttpAddRequestHeaders(hReq, w_auth, -1, WINHTTP_ADDREQ_FLAG_ADD);
        cJSON *cmd = cJSON_CreateObject();
        cJSON_AddStringToObject(cmd, "name", "chat");
        cJSON_AddStringToObject(cmd, "description", "พูดคุยกับ Gemini AI");
        cJSON *opts = cJSON_CreateArray();
        cJSON *opt = cJSON_CreateObject();
        cJSON_AddNumberToObject(opt, "type", 3);
        cJSON_AddStringToObject(opt, "name", "message");
        cJSON_AddStringToObject(opt, "description", "ข้อความที่จะส่ง");
        cJSON_AddBoolToObject(opt, "required", 1);
        cJSON_AddItemToArray(opts, opt);
        cJSON_AddItemToObject(cmd, "options", opts);
        char *body = cJSON_PrintUnformatted(cmd);
        WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (LPVOID)body, (DWORD)strlen(body), (DWORD)strlen(body), 0);
        WinHttpReceiveResponse(hReq, NULL);
        free(body); cJSON_Delete(cmd); WinHttpCloseHandle(hReq);
    }

    // Register /clear
    hReq = WinHttpOpenRequest(hConnect, L"POST", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (hReq) {
        WinHttpAddRequestHeaders(hReq, w_auth, -1, WINHTTP_ADDREQ_FLAG_ADD);
        cJSON *cmd = cJSON_CreateObject();
        cJSON_AddStringToObject(cmd, "name", "clear");
        cJSON_AddStringToObject(cmd, "description", "ล้างความจำบอทในห้องนี้");
        char *body = cJSON_PrintUnformatted(cmd);
        WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (LPVOID)body, (DWORD)strlen(body), (DWORD)strlen(body), 0);
        WinHttpReceiveResponse(hReq, NULL);
        free(body); cJSON_Delete(cmd); WinHttpCloseHandle(hReq);
    }

    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
}

void discord_send_message(DiscordBot *bot, const char *channel_id, const char *content) {
    HINTERNET hSession = WinHttpOpen(L"DiscordBotC/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET hConnect = WinHttpConnect(hSession, L"discord.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    wchar_t path[256];
    swprintf_s(path, 256, L"/api/v10/channels/%S/messages", channel_id);
    HINTERNET hReq = WinHttpOpenRequest(hConnect, L"POST", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (hReq) {
        char auth[256];
        sprintf_s(auth, 256, "Authorization: Bot %s\r\nContent-Type: application/json", bot->token);
        wchar_t w_auth[256];
        MultiByteToWideChar(CP_UTF8, 0, auth, -1, w_auth, 256);
        WinHttpAddRequestHeaders(hReq, w_auth, -1, WINHTTP_ADDREQ_FLAG_ADD);
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "content", content);
        char *js = cJSON_PrintUnformatted(p);
        WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (LPVOID)js, (DWORD)strlen(js), (DWORD)strlen(js), 0);
        WinHttpReceiveResponse(hReq, NULL);
        free(js); cJSON_Delete(p); WinHttpCloseHandle(hReq);
    }
    WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
}

void discord_send_interaction_response(DiscordBot *bot, const char *id, const char *tok, int type, const char *content) {
    HINTERNET hSession = WinHttpOpen(L"DiscordBotC/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET hConnect = WinHttpConnect(hSession, L"discord.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    wchar_t path[512];
    swprintf_s(path, 512, L"/api/v10/interactions/%S/%S/callback", id, tok);
    HINTERNET hReq = WinHttpOpenRequest(hConnect, L"POST", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (hReq) {
        wchar_t header[] = L"Content-Type: application/json";
        WinHttpAddRequestHeaders(hReq, header, -1, WINHTTP_ADDREQ_FLAG_ADD);
        cJSON *p = cJSON_CreateObject();
        cJSON_AddNumberToObject(p, "type", type);
        if (content) {
            cJSON *d = cJSON_CreateObject();
            cJSON_AddItemToObject(p, "data", d);
            cJSON_AddStringToObject(d, "content", content);
        }
        char *js = cJSON_PrintUnformatted(p);
        WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (LPVOID)js, (DWORD)strlen(js), (DWORD)strlen(js), 0);
        WinHttpReceiveResponse(hReq, NULL);
        free(js); cJSON_Delete(p); WinHttpCloseHandle(hReq);
    }
    WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
}

void discord_edit_interaction_response(DiscordBot *bot, const char *tok, const char *content) {
    HINTERNET hSession = WinHttpOpen(L"DiscordBotC/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET hConnect = WinHttpConnect(hSession, L"discord.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    wchar_t path[512];
    swprintf_s(path, 512, L"/api/v10/webhooks/%S/%S/messages/@original", bot->application_id, tok);
    HINTERNET hReq = WinHttpOpenRequest(hConnect, L"PATCH", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (hReq) {
        wchar_t header[] = L"Content-Type: application/json";
        WinHttpAddRequestHeaders(hReq, header, -1, WINHTTP_ADDREQ_FLAG_ADD);
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "content", content);
        char *js = cJSON_PrintUnformatted(p);
        WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (LPVOID)js, (DWORD)strlen(js), (DWORD)strlen(js), 0);
        WinHttpReceiveResponse(hReq, NULL);
        free(js); cJSON_Delete(p); WinHttpCloseHandle(hReq);
    }
    WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
}

void discord_cleanup(DiscordBot *bot) {
    if (bot->hWebSocket) WinHttpCloseHandle(bot->hWebSocket);
    if (bot->hConnect) WinHttpCloseHandle(bot->hConnect);
    if (bot->hSession) WinHttpCloseHandle(bot->hSession);
    free(bot->token);
    if (bot->application_id) free(bot->application_id);
}
