// build_osdev_dataset.c
// gcc -o build_osdev_dataset build_osdev_dataset.c -lcurl -lxml2 -lssl -lcrypto -pthread
// ./build_osdev_dataset [data_dir] [output.jsonl]

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <libxml/HTMLparser.h>
#include <libxml/xpath.h>
#include <curl/curl.h>

// === Настройки ===
#define MAX_PATH 1024
#define MAX_CONTENT (1024 * 1024)  // 1 MB
#define ESCAPE_BUF_SIZE (MAX_CONTENT * 6)
#define MAX_HASHES 10000
#define MAX_SOURCE_LEN 512

// === Внешние ===
extern const SiteConfig SITES[];
extern const size_t SITE_COUNT;

// === Глобальные ===
static char *seen_hashes[MAX_HASHES] = {0};
static size_t hash_count = 0;

// === Структуры ===
typedef struct {
    char *memory;
    size_t size;
} CurlWriteBuffer;

// === Вспомогательные функции ===

void trim_newlines(char *str) {
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r')) {
        str[--len] = '\0';
    }
    while (*str == '\n' || *str == '\r') str++;
}

int ends_with(const char* str, const char* suffix) {
    if (!str || !suffix) return 0;
    size_t len_str = strlen(str);
    size_t len_suffix = strlen(suffix);
    if (len_suffix > len_str) return 0;
    return strcmp(str + len_str - len_suffix, suffix) == 0;
}

int file_exists(const char* path) {
    struct stat buffer;
    return (stat(path, &buffer) == 0);
}

// === SHA256 (упрощённо) ===
// Предполагается, что compute_sha256() реализован в protocol.h
// Если нет — можно использовать OpenSSL или встроенный SHA-256 (код ниже опущён для краткости)

// === JSON escaping ===
void escape_json_string(const char* input, char* output) {
    const char* src = input;
    char* dst = output;
    while (*src) {
        switch (*src) {
            case '\"':  strcpy(dst, "\\\""); dst += 2; break;
            case '\\':  strcpy(dst, "\\\\"); dst += 2; break;
            case '\b':  strcpy(dst, "\\b");  dst += 2; break;
            case '\f':  strcpy(dst, "\\f");  dst += 2; break;
            case '\n':  strcpy(dst, "\\n");  dst += 2; break;
            case '\r':  strcpy(dst, "\\r");  dst += 2; break;
            case '\t':  strcpy(dst, "\\t");  dst += 2; break;
            default:
                if ((unsigned char)*src < 0x20) {
                    sprintf(dst, "\\u%04x", (unsigned char)*src);
                    dst += 6;
                } else {
                    *dst++ = *src;
                }
        }
        src++;
    }
    *dst = '\0';
}

int is_duplicate(const char *content) {
    if (!content || !*content) return 1;
    char *hash = compute_sha256(content);
    if (!hash) return 0;
    for (size_t i = 0; i < hash_count; i++) {
        if (seen_hashes[i] && strcmp(seen_hashes[i], hash) == 0) {
            free(hash);
            return 1;
        }
    }
    if (hash_count < MAX_HASHES) {
        seen_hashes[hash_count++] = hash;
    } else {
        free(hash);
    }
    return 0;
}

// === Парсинг HTML ===
char* extract_html_content(const char* html, const char* xpath_expr) {
    if (!html || !xpath_expr || !*xpath_expr) return NULL;

    htmlDocPtr doc = htmlReadMemory(html, strlen(html), NULL, NULL,
                                    HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING);
    if (!doc) return NULL;

    xmlXPathContextPtr context = xmlXPathNewContext(doc);
    if (!context) {
        xmlFreeDoc(doc);
        return NULL;
    }

    xmlXPathObjectPtr result = xmlXPathEvalExpression((xmlChar*)xpath_expr, context);
    if (!result || !result->nodesetval) {
        xmlXPathFreeContext(context);
        xmlFreeDoc(doc);
        return NULL;
    }

    xmlNodeSetPtr nodeset = result->nodesetval;
    xmlBufferPtr buffer = xmlBufferCreate();
    for (int i = 0; i < nodeset->nodeNr; i++) {
        xmlNodePtr node = nodeset->nodeTab[i];
        xmlNodeDump(buffer, doc, node, 0, 0);
        xmlBufferAdd(buffer, (xmlChar*)"\n", 1);
    }

    char* output = strdup((char*)xmlBufferContent(buffer));
    xmlBufferFree(buffer);
    xmlXPathFreeObject(result);
    xmlXPathFreeContext(context);
    xmlFreeDoc(doc);

    return output;
}

// === Скачивание URL ===
size_t curl_write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    CurlWriteBuffer *buf = (CurlWriteBuffer *)userp;
    char *ptr = realloc(buf->memory, buf->size + realsize + 1);
    if (!ptr) return 0;
    buf->memory = ptr;
    memcpy(&(buf->memory[buf->size]), contents, realsize);
    buf->size += realsize;
    buf->memory[buf->size] = 0;
    return realsize;
}

int download_url(const char* url, char* buffer, size_t size) {
    if (strncmp(url, "file://", 7) == 0) {
        FILE* f = fopen(url + 7, "rb");
        if (!f) return -1;
        size_t n = fread(buffer, 1, size - 1, f);
        fclose(f);
        buffer[n] = '\0';
        return 0;
    }

    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    CurlWriteBuffer chunk = {0};
    chunk.memory = malloc(1);
    chunk.size = 0;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "osdev-dataset-builder/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || chunk.size == 0 || chunk.size >= size) {
        free(chunk.memory);
        return -1;
    }

    strncpy(buffer, chunk.memory, size - 1);
    buffer[size - 1] = '\0';
    free(chunk.memory);
    return 0;
}

// === Генерация из sites ===
void process_sites(FILE* out, int *record_count) {
    for (size_t i = 0; i < SITE_COUNT; i++) {
        const char* url = SITES[i].url;
        const char* title_xpath = SITES[i].title_xpath;
        const char* content_xpath = SITES[i].content_xpath;
        const char* category = SITES[i].category;

        char raw_html[MAX_CONTENT];
        if (download_url(url, raw_html, sizeof(raw_html)) != 0) {
            fprintf(stderr, "⚠️  Skip (download failed): %s\n", url);
            continue;
        }

        char* title = title_xpath[0] ? extract_html_content(raw_html, title_xpath) : NULL;
        char* content = content_xpath[0] ? extract_html_content(raw_html, content_xpath) : NULL;

        if (!content || strlen(content) < 50) {
            free(title); free(content);
            continue;
        }

        // Нормализация
        trim_newlines(content);
        if (is_duplicate(content)) {
            free(title); free(content);
            continue;
        }

        char prompt[2048];
        if (title) {
            snprintf(prompt, sizeof(prompt), "Explain this %s concept in detail for an OS developer:\n\n%s", category, title);
            free(title);
        } else {
            snprintf(prompt, sizeof(prompt), "Explain this %s technical content for an OS developer.", category);
        }

        char esc_prompt[ESCAPE_BUF_SIZE];
        char esc_content[ESCAPE_BUF_SIZE];
        escape_json_string(prompt, esc_prompt);
        escape_json_string(content, esc_content);

        fprintf(out, "{\"messages\":[{\"role\":\"user\",\"content\":\"%s\"},{\"role\":\"assistant\",\"content\":\"%s\"}],\"metadata\":{\"source\":\"%s\",\"category\":\"%s\"}}\n",
                esc_prompt, esc_content, url, category);

        (*record_count)++;
        free(content);
    }
}

// === Генерация из manual/ ===
void process_manual_dir(const char* data_dir, FILE* out, int *record_count) {
    char manual_dir[MAX_PATH];
    snprintf(manual_dir, sizeof(manual_dir), "%s/manual", data_dir);

    DIR* dir = opendir(manual_dir);
    if (!dir) return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, ".", 1) == 0) continue;
        if (!ends_with(entry->d_name, "_prompt.txt")) continue;

        char base_name[MAX_PATH];
        strncpy(base_name, entry->d_name, sizeof(base_name) - 1);
        char* underscore = strstr(base_name, "_prompt.txt");
        if (!underscore) continue;
        *underscore = '\0';

        char prompt_path[MAX_PATH];
        char example_path[MAX_PATH];
        snprintf(prompt_path, sizeof(prompt_path), "%s/%s", manual_dir, entry->d_name);
        snprintf(example_path, sizeof(example_path), "%s/%s_example.c", manual_dir, base_name);

        if (!file_exists(example_path)) continue;

        char prompt_content[MAX_CONTENT];
        char example_content[MAX_CONTENT];
        if (read_file_content(prompt_path, prompt_content, sizeof(prompt_content)) != 0) continue;
        if (read_file_content(example_path, example_content, sizeof(example_content)) != 0) continue;

        if (is_duplicate(example_content)) continue;

        char esc_prompt[ESCAPE_BUF_SIZE];
        char esc_example[ESCAPE_BUF_SIZE];
        escape_json_string(prompt_content, esc_prompt);
        escape_json_string(example_content, esc_example);

        fprintf(out, "{\"messages\":[{\"role\":\"user\",\"content\":\"%s\"},{\"role\":\"assistant\",\"content\":\"%s\"}],\"metadata\":{\"source\":\"manual\",\"category\":\"C_OSDEV\"}}\n",
                esc_prompt, esc_example);

        (*record_count)++;
    }
    closedir(dir);
}

// === Основная функция ===
int main(int argc, char* argv[]) {
    const char* data_dir = "data";
    const char* output_path = "osdev_dataset.jsonl";

    if (argc >= 2) data_dir = argv[1];
    if (argc >= 3) output_path = argv[2];

    curl_global_init(CURL_GLOBAL_DEFAULT);

    FILE* out = fopen(output_path, "w");
    if (!out) {
        fprintf(stderr, "Error: cannot create output file '%s': %s\n", output_path, strerror(errno));
        return 1;
    }

    int record_count = 0;

    // 1. Обработка сайтов из config.h
    printf("🌐 Downloading and parsing %zu sites...\n", SITE_COUNT);
    process_sites(out, &record_count);

    // 2. Обработка ручных примеров
    printf("📂 Processing manual examples...\n");
    process_manual_dir(data_dir, out, &record_count);

    fclose(out);
    curl_global_cleanup();

    printf("✅ Dataset written to '%s'\n", output_path);
    printf("📊 Total records: %d\n", record_count);
    return 0;
}