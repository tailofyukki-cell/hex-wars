/* anim.c - 動画/アニメーションの読込と再生
 *
 * GIF は SDL2_image でメモリに展開して保持する。
 * mp4 等は ffmpeg で PNG 連番に変換してキャッシュし、再生時に1枚ずつ読む
 * （長い動画でもメモリを食わないようにするため）。
 */
#include "anim.h"
#include "app.h"
#include <SDL_image.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifdef _WIN32
#  include <windows.h>
#  include <direct.h>
#  define HW_MKDIR(p) _mkdir(p)
#else
#  include <sys/types.h>
#  define HW_MKDIR(p) mkdir((p), 0777)
#endif

/* 変換パラメータ（メモリ・変換時間とのバランス） */
#define ANIM_FPS        15
#define ANIM_MAX_W      720
#define ANIM_MAX_FRAMES 900     /* 15fps × 60秒 */

/* ユニット別キャッシュ */
static UnitAnim s_anim[MAX_UNIT_TYPES];
static int8_t   s_state[MAX_UNIT_TYPES];

/* パス別キャッシュ（報酬・エンドロール用） */
#define PATH_SLOTS 6
static struct {
    char     rel[128];
    UnitAnim ua;
    int8_t   state;      /* 0=未 / 1=OK / -1=失敗 */
} s_paths[PATH_SLOTS];

void uanim_init(void)
{
    /* GIF は追加初期化なしで読める。WebP は libwebp 系のDLLが別途必要。
     *
     * 【重要】WebP の初期化に失敗すると SDL2_image の内部状態が壊れ、
     * その後の PNG 読込（ユニットのスプライト）まで失敗するようになる。
     * そのため WebP を試したあとは必ず PNG を再初期化して復旧させること。 */
    IMG_Init(IMG_INIT_WEBP);
    IMG_Init(IMG_INIT_PNG);
}

static void free_one(UnitAnim *ua)
{
    if (ua->frames) {
        for (int i = 0; i < ua->count; i++)
            if (ua->frames[i]) SDL_DestroyTexture(ua->frames[i]);
        free(ua->frames);
    }
    free(ua->delays);
    if (ua->cached_tex) SDL_DestroyTexture(ua->cached_tex);
    memset(ua, 0, sizeof *ua);
    ua->cached_idx = -1;
}

void uanim_clear(void)
{
    for (int t = 0; t < MAX_UNIT_TYPES; t++) { free_one(&s_anim[t]); s_state[t] = 0; }
    for (int i = 0; i < PATH_SLOTS; i++) {
        free_one(&s_paths[i].ua);
        s_paths[i].rel[0] = '\0';
        s_paths[i].state = 0;
    }
}

void uanim_quit(void) { uanim_clear(); }

/* ------------------------------------------------------------------ */
/* 拡張子判定                                                          */
/* ------------------------------------------------------------------ */
static const char *ext_of(const char *p)
{
    const char *dot = strrchr(p, '.');
    return dot ? dot + 1 : "";
}

static bool ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (ca != cb) return false;
    }
    return *a == *b;
}

bool uanim_is_video_path(const char *rel)
{
    if (!rel || !rel[0]) return false;
    const char *e = ext_of(rel);
    return ieq(e, "mp4") || ieq(e, "webm") || ieq(e, "mov") ||
           ieq(e, "mkv") || ieq(e, "avi") || ieq(e, "m4v");
}

/* ------------------------------------------------------------------ */
/* ffmpeg による PNG 連番展開                                          */
/* ------------------------------------------------------------------ */

/* rel を安全なフォルダ名に（区切りと記号を _ に） */
static void sanitize(const char *rel, char *out, size_t n)
{
    size_t k = 0;
    for (const char *p = rel; *p && k + 1 < n; p++) {
        char c = *p;
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-';
        out[k++] = ok ? c : '_';
    }
    out[k] = '\0';
}

static void ensure_dir(const char *path)
{
    HW_MKDIR(path);
}

/* 親から順にフォルダを作る */
static void ensure_dir_chain(const char *path)
{
    char tmp[600];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char save = *p;
            *p = '\0';
            ensure_dir(tmp);
            *p = save;
        }
    }
    ensure_dir(tmp);
}

/* ffmpeg を同期実行。成功=true。ウィンドウは出さない */
static bool run_ffmpeg(const char *exe, const char *src, const char *outpat)
{
    char vf[160];
    snprintf(vf, sizeof vf, "fps=%d,scale='min(%d,iw)':-2",
             ANIM_FPS, ANIM_MAX_W);
#ifdef _WIN32
    char cmd[1800];
    snprintf(cmd, sizeof cmd,
             "\"%s\" -y -v error -i \"%s\" -vf \"%s\" -frames:v %d \"%s\"",
             exe, src, vf, ANIM_MAX_FRAMES, outpat);
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    memset(&pi, 0, sizeof pi);
    /* cmd.exe を介さず直接起動（CREATE_NO_WINDOW でコンソールを出さない） */
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return false;
    WaitForSingleObject(pi.hProcess, 120000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0;
#else
    char cmd[1800];
    snprintf(cmd, sizeof cmd,
             "\"%s\" -y -v error -i \"%s\" -vf \"%s\" -frames:v %d \"%s\"",
             exe, src, vf, ANIM_MAX_FRAMES, outpat);
    return system(cmd) == 0;
#endif
}

static bool file_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0;
}

/* mp4 等を展開して ua をディスクモードで初期化。成功=true */
static bool load_video(App *a, const char *rel, UnitAnim *ua)
{
    char src[600], key[160], dir[600], donep[700], pat[700];
    snprintf(src, sizeof src, "%sassets/%s", a->base_path, rel);
    if (!file_exists(src)) {
        SDL_Log("動画が見つかりません: %s", src);
        return false;
    }
    sanitize(rel, key, sizeof key);
    snprintf(dir, sizeof dir, "%scache/anim/%s", a->base_path, key);
    snprintf(donep, sizeof donep, "%s/done.txt", dir);
    snprintf(pat, sizeof pat, "%s/f%%04d.png", dir);

    int count = 0, fps = ANIM_FPS;
    bool cached = false;
    /* 既に展開済みで、元動画より新しければ再利用する */
    if (file_exists(donep)) {
        struct stat ss, ds;
        if (stat(src, &ss) == 0 && stat(donep, &ds) == 0 &&
            ds.st_mtime >= ss.st_mtime) {
            FILE *f = fopen(donep, "r");
            if (f) {
                if (fscanf(f, "%d %d", &count, &fps) == 2 && count > 0)
                    cached = true;
                fclose(f);
            }
        }
    }

    if (!cached) {
        ensure_dir_chain(dir);
        /* 既存の連番を消しておく（フレーム数が減るケースに備える） */
        for (int i = 1; i <= ANIM_MAX_FRAMES; i++) {
            char fp[700];
            snprintf(fp, sizeof fp, "%s/f%04d.png", dir, i);
            if (!file_exists(fp)) break;
            remove(fp);
        }
        /* ffmpeg を探す: 同梱 → PATH */
        char exe[600];
        snprintf(exe, sizeof exe, "%stools/ffmpeg/ffmpeg.exe", a->base_path);
        if (!file_exists(exe)) snprintf(exe, sizeof exe, "ffmpeg");
        SDL_Log("動画を変換中（初回のみ）: %s", rel);
        if (!run_ffmpeg(exe, src, pat)) {
            SDL_Log("ffmpeg での動画変換に失敗しました: %s\n"
                    "  mp4 を使うには ffmpeg が必要です（PATH か tools/ffmpeg/ffmpeg.exe）。",
                    rel);
            return false;
        }
        count = 0;
        for (int i = 1; i <= ANIM_MAX_FRAMES; i++) {
            char fp[700];
            snprintf(fp, sizeof fp, "%s/f%04d.png", dir, i);
            if (!file_exists(fp)) break;
            count = i;
        }
        if (count <= 0) {
            SDL_Log("動画の変換結果が空です: %s", rel);
            return false;
        }
        FILE *f = fopen(donep, "w");
        if (f) { fprintf(f, "%d %d\n", count, fps); fclose(f); }
    }

    /* 1枚目から原寸を得る */
    char f1[700];
    snprintf(f1, sizeof f1, "%s/f0001.png", dir);
    SDL_Surface *s1 = IMG_Load(f1);
    if (!s1) {
        SDL_Log("展開フレームが読めません: %s (%s)", f1, IMG_GetError());
        return false;
    }
    memset(ua, 0, sizeof *ua);
    ua->count = count;
    ua->fps = fps > 0 ? fps : ANIM_FPS;
    ua->w = s1->w;
    ua->h = s1->h;
    ua->total_ms = count * 1000 / ua->fps;
    ua->cached_idx = -1;
    snprintf(ua->dir, sizeof ua->dir, "%s", dir);
    SDL_FreeSurface(s1);
    return true;
}

/* GIF をメモリに展開。成功=true */
static bool load_gif(App *a, const char *rel, UnitAnim *ua)
{
    char path[600];
    snprintf(path, sizeof path, "%sassets/%s", a->base_path, rel);
    IMG_Animation *src = IMG_LoadAnimation(path);
    if (!src) {
        SDL_Log("アニメーション読込失敗: %s (%s)", path, IMG_GetError());
        return false;
    }
    memset(ua, 0, sizeof *ua);
    ua->cached_idx = -1;
    ua->count  = src->count;
    ua->w      = src->w;
    ua->h      = src->h;
    ua->frames = (SDL_Texture **)calloc((size_t)src->count, sizeof(SDL_Texture *));
    ua->delays = (int *)calloc((size_t)src->count, sizeof(int));
    if (!ua->frames || !ua->delays) {
        free_one(ua);
        IMG_FreeAnimation(src);
        return false;
    }
    for (int i = 0; i < src->count; i++) {
        if (src->frames[i]) {
            ua->frames[i] = SDL_CreateTextureFromSurface(a->ren, src->frames[i]);
            if (ua->frames[i]) {
                SDL_SetTextureBlendMode(ua->frames[i], SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(ua->frames[i], SDL_ScaleModeLinear);
            }
        }
        int d = src->delays ? src->delays[i] : 0;
        if (d < 16) d = 16;     /* 遅延0のGIF対策（約60fps上限） */
        ua->delays[i] = d;
        ua->total_ms += d;
    }
    IMG_FreeAnimation(src);
    if (ua->count <= 0 || ua->total_ms <= 0) { free_one(ua); return false; }
    return true;
}

static bool load_any(App *a, const char *rel, UnitAnim *ua)
{
    if (uanim_is_video_path(rel)) return load_video(a, rel, ua);
    return load_gif(a, rel, ua);
}

/* ------------------------------------------------------------------ */
/* 取得API                                                             */
/* ------------------------------------------------------------------ */
UnitAnim *uanim_get(App *a, int type)
{
    if (type < 0 || type >= MAX_UNIT_TYPES) return NULL;
    if (type >= a->game.n_types) return NULL;
    if (s_state[type] == 1) return &s_anim[type];
    if (s_state[type] == -1) return NULL;

    const char *rel = a->game.types[type].anim;
    if (!rel[0]) { s_state[type] = -1; return NULL; }
    if (!load_any(a, rel, &s_anim[type])) { s_state[type] = -1; return NULL; }
    s_state[type] = 1;
    return &s_anim[type];
}

UnitAnim *uanim_get_path(App *a, const char *rel)
{
    if (!rel || !rel[0]) return NULL;
    int free_i = -1;
    for (int i = 0; i < PATH_SLOTS; i++) {
        if (s_paths[i].state != 0 && !strcmp(s_paths[i].rel, rel))
            return s_paths[i].state == 1 ? &s_paths[i].ua : NULL;
        if (s_paths[i].state == 0 && free_i < 0) free_i = i;
    }
    if (free_i < 0) {   /* 空きが無ければ先頭を捨てる */
        free_i = 0;
        free_one(&s_paths[0].ua);
        s_paths[0].state = 0;
        s_paths[0].rel[0] = '\0';
    }
    snprintf(s_paths[free_i].rel, sizeof s_paths[free_i].rel, "%s", rel);
    if (!load_any(a, rel, &s_paths[free_i].ua)) {
        s_paths[free_i].state = -1;
        return NULL;
    }
    s_paths[free_i].state = 1;
    return &s_paths[free_i].ua;
}

/* ------------------------------------------------------------------ */
/* 再生                                                                */
/* ------------------------------------------------------------------ */
SDL_Texture *uanim_frame_at(App *a, UnitAnim *ua, int elapsed_ms)
{
    if (!ua || ua->count <= 0) return NULL;
    if (elapsed_ms < 0) elapsed_ms = 0;

    if (ua->dir[0]) {
        /* ディスクモード: 必要な1枚だけテクスチャ化して保持する */
        int idx = elapsed_ms * ua->fps / 1000;
        if (idx >= ua->count) idx = ua->count - 1;   /* 末尾で停止 */
        if (idx != ua->cached_idx) {
            char fp[700];
            snprintf(fp, sizeof fp, "%s/f%04d.png", ua->dir, idx + 1);
            SDL_Surface *s = IMG_Load(fp);
            if (s) {
                if (ua->cached_tex) SDL_DestroyTexture(ua->cached_tex);
                ua->cached_tex = SDL_CreateTextureFromSurface(a->ren, s);
                if (ua->cached_tex) {
                    SDL_SetTextureBlendMode(ua->cached_tex, SDL_BLENDMODE_BLEND);
                    SDL_SetTextureScaleMode(ua->cached_tex, SDL_ScaleModeLinear);
                }
                SDL_FreeSurface(s);
                ua->cached_idx = idx;
            }
        }
        return ua->cached_tex;
    }

    /* メモリモード（GIF） */
    int acc = 0;
    for (int i = 0; i < ua->count; i++) {
        acc += ua->delays[i];
        if (elapsed_ms < acc) return ua->frames[i];
    }
    return ua->frames[ua->count - 1];
}
