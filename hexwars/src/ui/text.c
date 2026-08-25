/* text.c - text_ja.def の読込とルックアップ */
#include "text.h"
#include <SDL.h>
#include "../data/parser.h"
#include <stdio.h>
#include <string.h>

#define MAX_TEXTS 512

typedef struct {
    char key[48];
    char val[192];
} TextEntry;

static TextEntry s_texts[MAX_TEXTS];
static int s_n_texts;

int text_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    s_n_texts = 0;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *key, *val;
        if (parser_split_line(line, &key, &val) != 1) continue;
        if (s_n_texts >= MAX_TEXTS) {
            /* 黙って打ち切るとキー名が画面に出て原因が分かりにくいので知らせる */
            SDL_Log("text_ja.def: 文言が多すぎます（最大%d件）。以降は無視されます",
                    MAX_TEXTS);
            break;
        }
        snprintf(s_texts[s_n_texts].key, sizeof s_texts[0].key, "%s", key);
        snprintf(s_texts[s_n_texts].val, sizeof s_texts[0].val, "%s", val);
        s_n_texts++;
    }
    fclose(f);
    return s_n_texts > 0 ? 0 : -1;
}

const char *tx(const char *key)
{
    for (int i = 0; i < s_n_texts; i++)
        if (!strcmp(s_texts[i].key, key))
            return s_texts[i].val;
    return key;
}
