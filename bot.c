#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

#include "telebot/include/telebot.h"
#include "llama.cpp/include/llama.h"

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <model.gguf> <chat_id> \"<prompt>\"\n", argv[0]);
        return 1;
    }

    const char *model_path = argv[1];
    long long int chat_id = atoll(argv[2]);
    const char *prompt = argv[3];

    bool ok = true;
    bool bot_created = false;
    bool model_loaded = false;
    bool ctx_created = false;
    llama_token *tokens = NULL;

    telebot_handler_t bot;
    llama_model *model = NULL;
    llama_context *ctx = NULL;

    char full_prompt[4096] = {0};

    /* ========== INIT LLaMA ========== */
    llama_backend_init();

    model = llama_model_load_from_file(model_path, llama_model_default_params());
    if (!model) {
        fprintf(stderr, "❌ Model load failed: %s\n", model_path);
        ok = false;
    } else {
        model_loaded = true;
    }

    if (ok) {
        llama_context_params ctx_params = llama_context_default_params();
        ctx_params.n_ctx = 2048;
        ctx_params.n_threads = 4;

        ctx = llama_init_from_model(model, ctx_params);
        if (!ctx) {
            fprintf(stderr, "❌ Context init failed\n");
            ok = false;
        } else {
            ctx_created = true;
        }
    }

    /* ========== TELEGRAM INIT ========== */
    char token_buf[256] = {0};
    if (ok) {
        FILE *f = fopen(".token", "r");
        if (!f) {
            perror("❌ .token");
            ok = false;
        } else {
            if (!fgets(token_buf, sizeof(token_buf), f)) {
                perror("❌ reading .token");
                ok = false;
            }
            fclose(f);
            token_buf[strcspn(token_buf, "\n")] = 0;
            if (token_buf[0] == '\0') {
                fprintf(stderr, "❌ .token is empty\n");
                ok = false;
            }
        }
    }

    if (ok) {
        if (telebot_create(&bot, token_buf) != TELEBOT_ERROR_NONE) {
            fprintf(stderr, "❌ Telegram bot init failed\n");
            ok = false;
        } else {
            bot_created = true;
        }
    }

    /* ========== BUILD PROMPT & TOKENIZE ========== */
    int32_t n_tokens = 0;
    const llama_vocab *vocab = model ? &model->vocab : NULL;

    if (ok) {
        int prompt_len = snprintf(full_prompt, sizeof(full_prompt),
            "<|begin_of_text|><|start_header_id|>user<|end_header_id|>\n\n%s<|eot_id|><|start_header_id|>assistant<|end_header_id|>\n\n",
            prompt);
        if (prompt_len <= 0 || (size_t)prompt_len >= sizeof(full_prompt)) {
            telebot_send_message(bot, chat_id, "❌ Prompt too long", "", false, false, 0, "");
            ok = false;
        } else {
            // Токенизация через vocab
            n_tokens = llama_tokenize(vocab, full_prompt, (int32_t)strlen(full_prompt), NULL, 0, true, false);
            if (n_tokens <= 0) {
                telebot_send_message(bot, chat_id, "❌ Tokenization failed", "", false, false, 0, "");
                ok = false;
            } else {
                tokens = malloc((size_t)n_tokens * sizeof(llama_token));
                if (!tokens) {
                    telebot_send_message(bot, chat_id, "❌ Out of memory", "", false, false, 0, "");
                    ok = false;
                } else {
                    int32_t actual_n = llama_tokenize(vocab, full_prompt, (int32_t)strlen(full_prompt), tokens, n_tokens, true, false);
                    if (actual_n != n_tokens || actual_n <= 0) {
                        telebot_send_message(bot, chat_id, "❌ Tokenization mismatch", "", false, false, 0, "");
                        ok = false;
                    }
                }
            }
        }
    }

    /* ========== GENERATION ========== */
    if (ok) {
        struct llama_sampler *smpl = llama_sampler_init_greedy();
        if (!smpl) {
            telebot_send_message(bot, chat_id, "❌ Sampler init failed", "", false, false, 0, "");
            ok = false;
        } else {
            llama_token eos_token = llama_vocab_eos(vocab);
            char response[2048] = {0};
            int32_t n_gen = 0;
            const int32_t max_tokens = 256;

            for (int32_t i = 0; i < max_tokens && ok; ++i) {
                llama_token new_token = llama_sampler_sample(smpl, ctx, -1);
                if (new_token == eos_token) break;

                char piece[64] = {0};
                int32_t n_piece = llama_token_to_piece(vocab, new_token, piece, sizeof(piece), 0, false);
                if (n_piece <= 0 || n_piece >= (int32_t)sizeof(piece)) break;
                if (n_gen + n_piece >= (int32_t)sizeof(response) - 1) break;

                memcpy(response + n_gen, piece, (size_t)n_piece);
                n_gen += n_piece;
                response[n_gen] = '\0';

                struct llama_batch next = llama_batch_get_one(&new_token, 1);
                if (llama_decode(ctx, next) != 0) {
                    ok = false;
                    break;
                }
            }

            llama_sampler_free(smpl);

            if (n_gen == 0) strcpy(response, "No response.");
            if (bot_created) telebot_send_message(bot, chat_id, response, "", false, false, 0, "");
        }
    }

    /* ========== CLEANUP ========== */
    free(tokens);
    if (bot_created) telebot_destroy(bot);
    if (ctx_created) llama_free(ctx);
    if (model_loaded) llama_model_free(model);
    llama_backend_free();

    return ok ? 0 : 1;
}
