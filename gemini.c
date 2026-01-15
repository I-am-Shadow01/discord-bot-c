#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <winhttp.h>
#include "gemini.h"
#include "cJSON.h"

// --- History Structures ---
typedef struct Message {
    char *role; // "user" or "model"
    char *content;
    struct Message *next;
} Message;

typedef struct ChannelHistory {
    char *channel_id;
    Message *head;
    Message *tail;
    int count;
    struct ChannelHistory *next;
} ChannelHistory;

static ChannelHistory *history_root = NULL;
static BotConfig *bot_config = NULL;

// --- History Internal Helpers ---

static ChannelHistory* get_or_create_history(const char *channel_id) {
    ChannelHistory *curr = history_root;
    while (curr) {
        if (strcmp(curr->channel_id, channel_id) == 0) return curr;
        curr = curr->next;
    }
    ChannelHistory *new_hist = (ChannelHistory*)calloc(1, sizeof(ChannelHistory));
    new_hist->channel_id = _strdup(channel_id);
    new_hist->next = history_root;
    history_root = new_hist;
    return new_hist;
}

static void add_message_to_history(ChannelHistory *hist, const char *role, const char *content) {
    Message *msg = (Message*)calloc(1, sizeof(Message));
    msg->role = _strdup(role);
    msg->content = _strdup(content);
    
    if (hist->tail) {
        hist->tail->next = msg;
        hist->tail = msg;
    } else {
        hist->head = msg;
        hist->tail = msg;
    }
    hist->count++;

    if (hist->count > 20) { // Keep last 20 messages
        Message *old = hist->head;
        hist->head = old->next;
        free(old->role);
        free(old->content);
        free(old);
        hist->count--;
    }
}

// --- API Implementation ---

void gemini_init(BotConfig *config) {
    bot_config = config;
}

void gemini_clear_history(const char *channel_id) {
    ChannelHistory *curr = history_root;
    ChannelHistory *prev = NULL;
    while (curr) {
        if (strcmp(curr->channel_id, channel_id) == 0) {
            Message *msg = curr->head;
            while (msg) {
                Message *next = msg->next;
                free(msg->role); free(msg->content); free(msg);
                msg = next;
            }
            free(curr->channel_id);
            if (prev) prev->next = curr->next;
            else history_root = curr->next;
            free(curr);
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

char* gemini_chat(const char *channel_id, const char *user_message, const char *username) {
    if (!bot_config || !bot_config->gemini_api_key) return _strdup("⚠️ มิ้งกี้หา API Key ไม่เจอค่ะ!");

    ChannelHistory *hist = get_or_create_history(channel_id);
    
    // Construct prompt with username for better context
    char full_user_msg[2048];
    snprintf(full_user_msg, sizeof(full_user_msg), "[%s]: %s", username ? username : "User", user_message);
    add_message_to_history(hist, "user", full_user_msg);

    // Build Request Payload
    cJSON *payload = cJSON_CreateObject();
    
    // System Instruction (v1beta)
    if (bot_config->system_prompt) {
        cJSON *si = cJSON_CreateObject();
        cJSON *siparts = cJSON_CreateArray();
        cJSON *sitext = cJSON_CreateObject();
        cJSON_AddStringToObject(sitext, "text", bot_config->system_prompt);
        cJSON_AddItemToArray(siparts, sitext);
        cJSON_AddItemToObject(si, "parts", siparts);
        cJSON_AddItemToObject(payload, "system_instruction", si);
    }

    cJSON *contents = cJSON_CreateArray();
    cJSON_AddItemToObject(payload, "contents", contents);

    Message *curr = hist->head;
    while (curr) {
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddStringToObject(entry, "role", curr->role);
        cJSON *parts = cJSON_CreateArray();
        cJSON *part = cJSON_CreateObject();
        cJSON_AddStringToObject(part, "text", curr->content);
        cJSON_AddItemToArray(parts, part);
        cJSON_AddItemToObject(entry, "parts", parts);
        cJSON_AddItemToArray(contents, entry);
        curr = curr->next;
    }

    // Safety Settings
    cJSON *ss = cJSON_CreateArray();
    const char *cats[] = {"HARM_CATEGORY_HARASSMENT", "HARM_CATEGORY_HATE_SPEECH", "HARM_CATEGORY_SEXUALLY_EXPLICIT", "HARM_CATEGORY_DANGEROUS_CONTENT"};
    for(int i=0; i<4; i++) {
        cJSON *s = cJSON_CreateObject();
        cJSON_AddStringToObject(s, "category", cats[i]);
        cJSON_AddStringToObject(s, "threshold", "BLOCK_NONE");
        cJSON_AddItemToArray(ss, s);
    }
    cJSON_AddItemToObject(payload, "safetySettings", ss);

    char *json_body = cJSON_PrintUnformatted(payload);
    
    // Send REST Call
    char *replyText = NULL;
    HINTERNET hSession = WinHttpOpen(L"GeminiBot/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET hConnect = WinHttpConnect(hSession, L"generativelanguage.googleapis.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    
    wchar_t path[2048];
    const char *model = bot_config->model ? bot_config->model : "gemini-1.5-flash";
    swprintf_s(path, 2048, L"/v1beta/models/%S:generateContent?key=%S", model, bot_config->gemini_api_key);

    HINTERNET hReq = WinHttpOpenRequest(hConnect, L"POST", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    
    if (hReq) {
        wchar_t header[] = L"Content-Type: application/json";
        WinHttpAddRequestHeaders(hReq, header, -1, WINHTTP_ADDREQ_FLAG_ADD);
        
        if (WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (LPVOID)json_body, (DWORD)strlen(json_body), (DWORD)strlen(json_body), 0)) {
            if (WinHttpReceiveResponse(hReq, NULL)) {
                DWORD dwSize = 0, total = 0;
                char *resp = NULL;
                while (WinHttpQueryDataAvailable(hReq, &dwSize) && dwSize > 0) {
                    char *chunk = malloc(dwSize);
                    DWORD read = 0;
                    if (WinHttpReadData(hReq, chunk, dwSize, &read)) {
                        resp = realloc(resp, total + read + 1);
                        memcpy(resp + total, chunk, read);
                        total += read;
                        resp[total] = 0;
                    }
                    free(chunk);
                }
                if (resp) {
                    cJSON *rj = cJSON_Parse(resp);
                    if (rj) {
                        cJSON *err = cJSON_GetObjectItem(rj, "error");
                        if (err) {
                            cJSON *msg = cJSON_GetObjectItem(err, "message");
                            if (msg) {
                                char buf[512]; snprintf(buf, sizeof(buf), "❌ [Gemini Error] %s", msg->valuestring);
                                replyText = _strdup(buf);
                            }
                        } else {
                            cJSON *cands = cJSON_GetObjectItem(rj, "candidates");
                            if (cands && cJSON_GetArraySize(cands) > 0) {
                                cJSON *parts = cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetArrayItem(cands, 0), "content"), "parts");
                                if (parts && cJSON_GetArraySize(parts) > 0) {
                                    cJSON *txt = cJSON_GetObjectItem(cJSON_GetArrayItem(parts, 0), "text");
                                    if (txt) {
                                        replyText = _strdup(txt->valuestring);
                                        add_message_to_history(hist, "model", replyText);
                                    }
                                }
                            }
                        }
                        cJSON_Delete(rj);
                    }
                    free(resp);
                }
            }
        }
        WinHttpCloseHandle(hReq);
    }
    
    WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
    free(json_body); cJSON_Delete(payload);

    return replyText ? replyText : _strdup("⚠️ ดูเหมือน Gemini จะเงียบไปค่ะ ลองอีกทีนะคะ!");
}
