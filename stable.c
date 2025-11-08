#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <curl/curl.h>
#include "telebot/include/telebot.h"

struct memory {
    char *response;
    size_t size;
};

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct memory *mem = (struct memory *)userp;
    char *ptr = realloc(mem->response, mem->size + realsize + 1);
    if (ptr == NULL) return 0;
    mem->response = ptr;
    memcpy(&(mem->response[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->response[mem->size] = 0;
    return realsize;
}

char *http_get(const char *url) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    struct memory chunk = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
        free(chunk.response);
        return NULL;
    }
    return chunk.response;
}

void get_time(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *utc = gmtime(&now);

    // Москва = UTC+3
    time_t msk_time = now + 3 * 3600;
    struct tm *msk = gmtime(&msk_time);

    snprintf(buffer, size,
        "🕒 Время сейчас:\n"
        "• UTC: %02d:%02d\n"
        "• MSK: %02d:%02d",
        utc->tm_hour, utc->tm_min,
        msk->tm_hour, msk->tm_min);
}

int main() {
    printf("🚀 OXXYEN Bot v1.5 — чистый C\n");
    printf("──────────────────────────────\n");

    // Загружаем токен
    FILE *fp = fopen(".token", "r");
    if (!fp) { fprintf(stderr, "❌ Нет .token\n"); return -1; }
    char token[512]; fscanf(fp, "%s", token); fclose(fp);

    telebot_handler_t handle;
    if (telebot_create(&handle, token) != TELEBOT_ERROR_NONE) {
        fprintf(stderr, "❌ Ошибка инициализации Telebot\n");
        return -1;
    }

    telebot_user_t me;
    telebot_get_me(handle, &me);
    printf("✅ Запущен: %s (@%s)\n", me.first_name, me.username);
    telebot_put_me(&me);

    srand(time(NULL));

    const char *answers[] = {
        "Да 🎯", "Нет ❌", "Возможно 🤔", "Позже ⏳",
        "Определённо да ✅", "Лучше не знать 😶", "Без сомнений 💪"
    };
    int answers_count = sizeof(answers)/sizeof(answers[0]);

    int offset = -1, count;
    telebot_update_t *updates;

    while (1) {
        telebot_get_updates(handle, offset, 20, 0, NULL, 0, &updates, &count);
        for (int i = 0; i < count; i++) {
            telebot_message_t msg = updates[i].message;
            if (!msg.text) continue;

            long long chat_id = msg.chat->id;
            printf("📩 [%s]: %s\n", msg.from->first_name, msg.text);

            if (strcmp(msg.text, "/start") == 0) {
                char reply[2048];
                snprintf(reply, sizeof(reply),
                    "👋 Привет, %s!\n\n"
                    "Я — <b>OXXYEN Bot</b> 🧠\n"
                    "Быстрый, минималистичный и написан на чистом C.\n\n"
                    "⚙️ Команды:\n"
                    "• /help — помощь\n"
                    "• /dice — бросок кубика 🎲\n"
                    "• /8ball — магический шар 🎱\n"
                    "• /ip — показать IP 🌐\n"
                    "• /time — текущее время 🕒",
                    msg.from->first_name);
                telebot_send_message(handle, chat_id, reply, "HTML", false, false, msg.message_id, "");
            }
            else if (strcmp(msg.text, "/help") == 0) {
                telebot_send_message(handle, chat_id,
                    "📘 <b>Помощь</b>\n\n"
                    "• /start — приветствие\n"
                    "• /help — это сообщение\n"
                    "• /dice — бросить кубик 🎲\n"
                    "• /8ball — спросить судьбу 🎱\n"
                    "• /ip — показать IP 🌐\n"
                    "• /time — текущее время 🕒",
                    "HTML", false, false, msg.message_id, "");
            }
            else if (strcmp(msg.text, "/dice") == 0) {
                telebot_send_dice(handle, chat_id, false, 0, "");
            }
            else if (strcmp(msg.text, "/8ball") == 0) {
                int r = rand() % answers_count;
                telebot_send_message(handle, chat_id, answers[r], "", false, false, msg.message_id, "");
            }
            else if (strcmp(msg.text, "/ip") == 0) {
                char *resp = http_get("https://ipinfo.io/ip");
                if (resp) {
                    char reply[256];
                    snprintf(reply, sizeof(reply), "🌐 Твой IP: <code>%s</code>", resp);
                    telebot_send_message(handle, chat_id, reply, "HTML", false, false, msg.message_id, "");
                    free(resp);
                } else {
                    telebot_send_message(handle, chat_id, "⚠️ Не удалось получить IP.", "", false, false, msg.message_id, "");
                }
            }
            else if (strcmp(msg.text, "/time") == 0) {
                char buffer[256];
                get_time(buffer, sizeof(buffer));
                telebot_send_message(handle, chat_id, buffer, "", false, false, msg.message_id, "");
            }
            else {
                telebot_send_message(handle, chat_id,
                    "🤖 Неизвестная команда. Введите /help.", "", false, false, msg.message_id, "");
            }

            offset = updates[i].update_id + 1;
        }
        telebot_put_updates(updates, count);
        sleep(1);
    }

    telebot_destroy(handle);
    return 0;
}
