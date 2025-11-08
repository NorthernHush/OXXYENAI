// web_parser.c — Senior-grade static content scraper for C/Linux/IT
// Privacy is a feature. Intelligence is a discipline.

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <curl/curl.h>
#include <ctype.h>
#include <libxml/HTMLparser.h>
#include <libxml/xpath.h>
#include <libxml/tree.h>
#include <openssl/sha.h>

// ! EXAMPLE FOR DATASET.C

// ==================== CONFIG ====================

typedef struct {
    const char *url;
    const char *title_xpath;
    const char *content_xpath;
    const char *category;
} SiteConfig;

static const char *TOPIC_KEYWORDS[] = {
    "pointer", "memory", "malloc", "free", "kernel", "system call",
    "file", "process", "thread", "socket", "pipe", "fork", "exec",
    "linux", "unix", "posix", "buffer", "stack", "heap", "segmentation",
    "compiler", "linker", "assembly", "syscall", "descriptor"
};
static const size_t KEYWORD_COUNT = sizeof(TOPIC_KEYWORDS) / sizeof(TOPIC_KEYWORDS[0]);

// ==================== UTILS ====================

struct MemoryStruct {
    char *memory;
    size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) return 0;
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    return realsize;
}

char *escape_json(const char *input) {
    if (!input) return strdup("");
    size_t len = strlen(input);
    char *escaped = malloc(len * 6 + 3);
    if (!escaped) return NULL;
    char *p = escaped;
    for (const char *s = input; *s; s++) {
        switch (*s) {
            case '"':  p += sprintf(p, "\\\""); break;
            case '\\': p += sprintf(p, "\\\\"); break;
            case '\n': p += sprintf(p, "\\n");  break;
            case '\r': p += sprintf(p, "\\r");  break;
            case '\t': p += sprintf(p, "\\t");  break;
            default:
                if ((unsigned char)*s < 0x20) {
                    p += sprintf(p, "\\u%04x", (unsigned char)*s);
                } else {
                    *p++ = *s;
                }
        }
    }
    *p = '\0';
    return escaped;
}

void extract_text_recursive(xmlNode *node, xmlBufferPtr buf) {
    if (!node) return;
    if (node->type == XML_ELEMENT_NODE) {
        const char *name = (const char *)node->name;
        if (name && (!strcmp(name, "script") || !strcmp(name, "style") || !strcmp(name, "nav") || !strcmp(name, "footer"))) {
            return;
        }
    }
    if (node->type == XML_TEXT_NODE || node->type == XML_CDATA_SECTION_NODE) {
        xmlNodeBufGetContent(buf, node);
    }
    for (xmlNode *child = node->children; child; child = child->next) {
        extract_text_recursive(child, buf);
    }
}

char *get_clean_text(xmlNode *node) {
    if (!node) return NULL;
    xmlBufferPtr buf = xmlBufferCreate();
    if (!buf) return NULL;
    extract_text_recursive(node, buf);
    char *raw = (char *)xmlBufferContent(buf);
    if (!raw || xmlBufferLength(buf) == 0) {
        xmlBufferFree(buf);
        return NULL;
    }

    size_t len = strlen(raw);
    char *clean = malloc(len + 1);
    if (!clean) { xmlBufferFree(buf); return NULL; }
   
    char *content;
    if(strlen(content) > 2500) {
        char *trimmed = malloc(2501);
        if(trimmed) {
            strncpy(trimmed, content, 2500);
            trimmed[2500] = '\0';
            char *last_space = strrchr(trimmed, ' ');
            if(last_space && last_space > trimmed + 2300) {
                *last_space = '\0';
            }
            free(content);
            content = trimmed;
        }
    }

    char *p = clean;
    int space = 1;
    for (size_t i = 0; i < len; i++) {
        if (raw[i] == ' ' || raw[i] == '\t' || raw[i] == '\n' || raw[i] == '\r') {
            if (!space) { *p++ = ' '; space = 1; }
        } else {
            *p++ = raw[i]; space = 0;
        }
    }
    *p = '\0';
    xmlBufferFree(buf);
    return clean;
}

size_t word_count(const char *text) {
    if (!text) return 0;
    size_t count = 0;
    int in_word = 0;
    for (const char *s = text; *s; s++) {
        if (*s == ' ' || *s == '\t' || *s == '\n') {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            count++;
        }
    }
    return count;
}

int is_relevant_content(const char *text) {
    if (!text) return 0;
    char *lower = strdup(text);
    if (!lower) return 0;
    for (char *p = lower; *p; p++) *p = tolower(*p);

    int matches = 0;
    for (size_t i = 0; i < KEYWORD_COUNT; i++) {
        if (strstr(lower, TOPIC_KEYWORDS[i])) matches++;
        if (matches >= 2) break;
    }
    free(lower);
    return matches >= 2;
}

char *compute_sha256(const char *str) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)str, strlen(str), hash);
    char *output = malloc(65);
    if (!output) return NULL;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(output + (i * 2), "%02x", hash[i]);
    }
    output[64] = '\0';
    return output;
}

// Simple hash store for deduplication (max 1000)
#define MAX_HASHES 1000
static char *seen_hashes[MAX_HASHES] = {0};
static size_t hash_count = 0;

int is_duplicate(const char *content) {
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

// ==================== PARSING ====================

char *try_extract_content(htmlDocPtr doc, const char *xpath_expr) {
    if (!xpath_expr) return NULL;
    xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
    if (!ctx) return NULL;
    xmlXPathObjectPtr obj = xmlXPathEvalExpression((xmlChar*)xpath_expr, ctx);
    char *result = NULL;
    if (obj && obj->nodesetval && obj->nodesetval->nodeNr > 0) {
        result = get_clean_text(obj->nodesetval->nodeTab[0]);
    }
    xmlXPathFreeObject(obj);
    xmlXPathFreeContext(ctx);
    return result;
}

int parse_site(const SiteConfig *cfg, FILE *json_file, int *first) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    struct MemoryStruct chunk = {0};
    chunk.memory = malloc(1);
    chunk.size = 0;

    curl_easy_setopt(curl, CURLOPT_URL, cfg->url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) { free(chunk.memory); return 0; }

    htmlDocPtr doc = htmlReadMemory(chunk.memory, chunk.size, cfg->url, NULL,
                                    HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING);
    free(chunk.memory);
    if (!doc) return 0;

    // Title
    char *title = NULL;
    if (cfg->title_xpath) {
        title = try_extract_content(doc, cfg->title_xpath);
    }
    if (!title || strlen(title) < 5) {
        free(title);
        title = strdup("Без названия");
    }

    // Content: multiple strategies
    char *content = NULL;
    const char *fallback_xpaths[] = {
        "//article//p",
        "//main//p",
        "//div[@class='content']//p",
        "//div[@id='content']//p",
        "//div[contains(@class,'post')]//p",
        "//body//p"
    };
    size_t n_fallback = sizeof(fallback_xpaths) / sizeof(fallback_xpaths[0]);

    if (cfg->content_xpath) {
        content = try_extract_content(doc, cfg->content_xpath);
    }
    if (!content || strlen(content) < 300) {
        for (size_t i = 0; i < n_fallback && (!content || strlen(content) < 300); i++) {
            free(content);
            content = try_extract_content(doc, fallback_xpaths[i]);
        }
    }
    if (!content || strlen(content) < 300) {
        xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
        xmlXPathObjectPtr obj = xmlXPathEvalExpression((xmlChar*)"//body", ctx);
        if (obj && obj->nodesetval && obj->nodesetval->nodeNr > 0) {
            free(content);
            content = get_clean_text(obj->nodesetval->nodeTab[0]);
        }
        xmlXPathFreeObject(obj);
        xmlXPathFreeContext(ctx);
    }

    xmlFreeDoc(doc);

    if (!content || strlen(content) < 300) {
        free(title); free(content);
        return 0;
    }

    if (word_count(content) < 50 || !is_relevant_content(content)) {
        free(title); free(content);
        return 0;
    }

    if (is_duplicate(content)) {
        free(title); free(content);
        return 0;
    }

    // Output
    char *esc_title = escape_json(title);
    char *esc_content = escape_json(content);
    char *esc_url = escape_json(cfg->url);
    char *esc_cat = escape_json(cfg->category);

    if (!esc_title || !esc_content || !esc_url || !esc_cat) {
        free(title); free(content);
        free(esc_title); free(esc_content); free(esc_url); free(esc_cat);
        return -1;
    }

    if (!*first) fprintf(json_file, ",\n");
    *first = 0;

    fprintf(json_file, "  {\n");
    fprintf(json_file, "    \"url\": \"%s\",\n", esc_url);
    fprintf(json_file, "    \"title\": \"%s\",\n", esc_title);
    fprintf(json_file, "    \"category\": \"%s\",\n", esc_cat);
    fprintf(json_file, "    \"language\": \"en\",\n");
    fprintf(json_file, "    \"word_count\": %zu,\n", word_count(content));
    fprintf(json_file, "    \"content\": \"%s\"\n", esc_content);
    fprintf(json_file, "  }");

    free(title); free(content);
    free(esc_title); free(esc_content); free(esc_url); free(esc_cat);
    return 1;
}

// ==================== SOURCES ====================

static const SiteConfig SITES[] = {
    // C Programming
    {"https://www.cprogramming.com/tutorial/c/lesson1.html", "//h1", "//div[@id='content']//p", "C"},
    {"https://www.cprogramming.com/tutorial/c/lesson2.html", "//h1", "//div[@id='content']//p", "C"},
    {"https://www.cprogramming.com/tutorial/c/lesson3.html", "//h1", "//div[@id='content']//p", "C"},
    {"https://www.cs.cf.ac.uk/Dave/C/CE.html", "//h2[1]", "//blockquote | //p", "C"},
    {"https://www.gnu.org/software/gnu-c-manual/gnu-c-manual.html", "//h1", "//div[@class='chapter']//p", "C"},

    // OSDEV for DATASET

    {"https://wiki.osdev.org/Main_Page"},


    
    // ==========

    // Linux
    {"https://www.tldp.org/LDP/intro-linux/html/index.html", "//title", "//div[@class='toc']//following::p", "Linux"},
    {"https://www.tldp.org/LDP/Bash-Beginners-Guide/html/index.html", "//h1", "//div[@class='section']//p", "Linux"},
    {"https://www.tldp.org/LDP/abs/html/", "//h1", "//div[@class='section']//p", "Linux"},
    {"https://linux.die.net/", "//h1", "//p", "Linux"},
    {"https://www.linuxtopia.org/online_books/linux_administrator_guide/", "//h1", "//div[@class='section']//p", "Linux"},

    // System Calls & Man Pages
    {"https://man7.org/linux/man-pages/man2/intro.2.html", "//h1", "//div[@class='section']//p", "System"},
    {"https://man7.org/linux/man-pages/man2/fork.2.html", "//h1", "//div[@class='section']//p", "System"},
    {"https://man7.org/linux/man-pages/man2/execve.2.html", "//h1", "//div[@class='section']//p", "System"},
    {"https://man7.org/linux/man-pages/man3/malloc.3.html", "//h1", "//div[@class='section']//p", "C"},
    {"https://man7.org/linux/man-pages/man7/signal.7.html", "//h1", "//div[@class='section']//p", "System"},

    // Kernel & Low-level
    {"https://www.win.tue.nl/~aeb/linux/lk/lk.html", "//h1", "//ul/following::p", "Kernel"},

    // Beej's Guides
    {"https://beej.us/guide/bgnet/html/", "//h1", "//div[@class='refsect1']//p | //p", "Networking"},
    {"https://beej.us/guide/bgipc/html/", "//h1", "//div[@class='refsect1']//p | //p", "IPC"},

    // POSIX
    {"https://pubs.opengroup.org/onlinepubs/9699919799/functions/V2_chap02.html", "//h1", "//div[@class='section']//p", "POSIX"},
    {"https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap03.html", "//h1", "//div[@class='section']//p", "POSIX"},

    // BSD
    {"https://docs.freebsd.org/en/books/handbook/introduction/", "//h1", "//div[@class='section']//p", "BSD"},
    {"https://docs.freebsd.org/en/books/handbook/basics/", "//h1", "//div[@class='section']//p", "BSD"},
    {"https://www.openbsd.org/faq/faq1.html", "//h1", "//blockquote | //p", "BSD"},
    {"https://www.openbsd.org/faq/faq2.html", "//h1", "//blockquote | //p", "BSD"},

    // Linux.org
    {"https://www.linux.org/docs/", "//h1", "//p", "Linux"},
    {"https://www.linux.org/threads/linux-file-system-hierarchy.1234/", "//h1", "//div[@class='message']//p", "Linux"}
};

static const size_t SITE_COUNT = sizeof(SITES) / sizeof(SITES[0]);

// ==================== MAIN ====================

int main(void) {
    printf("parse web-site 2500!\n");
    FILE *json_file = fopen("output.json", "w");
    if (!json_file) {
        perror("Не удалось создать output.json");
        return EXIT_FAILURE;
    }
    fprintf(json_file, "[\n");
    int first = 1;

    curl_global_init(CURL_GLOBAL_DEFAULT);

    for (size_t i = 0; i < SITE_COUNT; i++) {
        printf("Парсинг [%zu/%zu]: %s\n", i + 1, SITE_COUNT, SITES[i].url);
        int result = parse_site(&SITES[i], json_file, &first);
        if (result > 0) {
            printf("  ✅ Успешно\n");
        } else {
            printf("  ⚠️  Пропущено\n");
        }
        struct timespec ts = { .tv_sec = 1, .tv_nsec = 0 };
        nanosleep(&ts, NULL);
    }

    fprintf(json_file, "\n]\n");
    fclose(json_file);
    curl_global_cleanup();

    printf("\n✅ Парсинг завершён. Результат: output.json\n");
    printf("ℹ️  Собрано до %zu уникальных статей по C, Linux и системному программированию.\n", hash_count);
    return EXIT_SUCCESS;
}
