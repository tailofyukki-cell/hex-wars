/* sound.c - SDL2_mixer による BGM/SE 再生
 *
 * 音源の一覧は data/audio.def に書く（ファイル名も拡張子も自由）。
 * audio.def が無い/壊れている場合は組み込みの既定リストで動く。 */
#include "sound.h"
#include "../data/parser.h"
#include <SDL.h>
#include <SDL_mixer.h>
#include <stdio.h>
#include <string.h>

static bool s_enabled = false;
static Mix_Chunk *s_se[SE_COUNT];
static Mix_Music *s_mus[HWM_COUNT];
static int s_current_mus = HWM_NONE;
static char s_base[512];        /* SEセット切替で読み直すため保持 */
static int  s_se_set = 0;

/* --- audio.def の内容 --- */
/* 各SE役割のファイル名（セットのフォルダからの相対） */
static char s_se_file[SE_COUNT][96];
/* 効果音セット: 表示名と assets/ からのフォルダ */
static char s_set_name[MAX_SE_SETS][48];
static char s_set_dir[MAX_SE_SETS][96];
static int  s_n_sets = 0;
/* 固定枠BGMと戦闘BGM（assets/ からの相対パス） */
static char s_mus_file[HWM_COUNT][128];
static char s_battle_name[MAX_BATTLE_TRACKS][48];
static int  s_n_battle = 0;

/* SE の役割名（audio.def の [se 名前] と対応。並びは SeId と同じ） */
static const char *SE_KEYS[SE_COUNT] = {
    "CURSOR", "OK", "CANCEL",
    "MOVE_FOOT", "MOVE_VEHICLE", "MOVE_AIR",
    "SHOT", "EXPLOSION", "CAPTURE", "TURN",
};
/* audio.def が読めなかったときの既定 */
static const char *SE_DEFAULT[SE_COUNT] = {
    "se_cursor.wav", "se_ok.wav", "se_cancel.wav",
    "se_move_foot.wav", "se_move_vehicle.wav", "se_move_air.wav",
    "se_shot.wav", "se_explosion.wav", "se_capture.wav", "se_turn.wav",
};

static void audio_defaults(void)
{
    for (int i = 0; i < SE_COUNT; i++)
        snprintf(s_se_file[i], sizeof s_se_file[i], "%s", SE_DEFAULT[i]);
    s_n_sets = 1;
    snprintf(s_set_name[0], sizeof s_set_name[0], "%s", "標準");
    snprintf(s_set_dir[0], sizeof s_set_dir[0], "%s", "sfx");

    memset(s_mus_file, 0, sizeof s_mus_file);
    snprintf(s_mus_file[HWM_TITLE],   sizeof s_mus_file[0], "bgm/bgm_title.wav");
    snprintf(s_mus_file[HWM_VICTORY], sizeof s_mus_file[0], "bgm/bgm_victory.wav");
    snprintf(s_mus_file[HWM_DEFEAT],  sizeof s_mus_file[0], "bgm/bgm_defeat.wav");
    snprintf(s_mus_file[HWM_ENDING],  sizeof s_mus_file[0], "bgm/bgm_ending.wav");
    s_n_battle = 0;
}

/* data/audio.def を読む。書式は他の .def と同じ INI 風。
 *   [se CURSOR]   file = se_cursor.wav        … 役割ごとのファイル名
 *   [seset]       name = 標準  dir = sfx      … 効果音セット（複数可）
 *   [music TITLE] file = bgm/xxx.ogg          … 固定枠BGM
 *   [battle]      file = bgm/yyy.ogg  name = 進撃   … 戦闘BGM（複数可・順に並ぶ）
 * 戻り値 0=読めた */
static int load_audio_def(const char *base_path)
{
    char path[600];
    snprintf(path, sizeof path, "%sdata/audio.def", base_path);
    FILE *f = fopen(path, "rb");
    if (!f) {
        SDL_Log("audio.def が読めません: %s（既定の音源で続行）", path);
        return -1;
    }
    /* セクション種別: 0=なし 1=se 2=seset 3=music 4=battle */
    int kind = 0, idx = -1;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *key, *val;
        int r = parser_split_line(line, &key, &val);
        if (r == 0) continue;
        if (r == 2) {
            kind = 0; idx = -1;
            if (!strncmp(key, "se ", 3)) {
                for (int i = 0; i < SE_COUNT; i++)
                    if (!strcmp(key + 3, SE_KEYS[i])) { kind = 1; idx = i; break; }
                if (idx < 0) SDL_Log("audio.def: 未知のSE名 [%s]", key);
            } else if (!strcmp(key, "seset")) {
                if (s_n_sets < MAX_SE_SETS) {
                    kind = 2; idx = s_n_sets++;
                    s_set_name[idx][0] = 0;
                    s_set_dir[idx][0] = 0;
                } else SDL_Log("audio.def: 効果音セットが多すぎます（最大%d）", MAX_SE_SETS);
            } else if (!strncmp(key, "music ", 6)) {
                if      (!strcmp(key + 6, "TITLE"))   { kind = 3; idx = HWM_TITLE; }
                else if (!strcmp(key + 6, "VICTORY")) { kind = 3; idx = HWM_VICTORY; }
                else if (!strcmp(key + 6, "DEFEAT"))  { kind = 3; idx = HWM_DEFEAT; }
                else if (!strcmp(key + 6, "ENDING"))  { kind = 3; idx = HWM_ENDING; }
                else SDL_Log("audio.def: 未知の music 名 [%s]", key);
            } else if (!strcmp(key, "battle")) {
                if (s_n_battle < MAX_BATTLE_TRACKS) {
                    kind = 4; idx = s_n_battle++;
                    s_battle_name[idx][0] = 0;
                    s_mus_file[HWM_BATTLE0 + idx][0] = 0;
                } else SDL_Log("audio.def: 戦闘BGMが多すぎます（最大%d）", MAX_BATTLE_TRACKS);
            } else {
                SDL_Log("audio.def: 未知のセクション [%s]", key);
            }
            continue;
        }
        if (idx < 0) continue;
        if (kind == 1 && !strcmp(key, "file"))
            snprintf(s_se_file[idx], sizeof s_se_file[0], "%s", val);
        else if (kind == 2 && !strcmp(key, "name"))
            snprintf(s_set_name[idx], sizeof s_set_name[0], "%s", val);
        else if (kind == 2 && !strcmp(key, "dir"))
            snprintf(s_set_dir[idx], sizeof s_set_dir[0], "%s", val);
        else if (kind == 3 && !strcmp(key, "file"))
            snprintf(s_mus_file[idx], sizeof s_mus_file[0], "%s", val);
        else if (kind == 4 && !strcmp(key, "file"))
            snprintf(s_mus_file[HWM_BATTLE0 + idx], sizeof s_mus_file[0], "%s", val);
        else if (kind == 4 && !strcmp(key, "name"))
            snprintf(s_battle_name[idx], sizeof s_battle_name[0], "%s", val);
    }
    fclose(f);

    if (s_n_sets == 0) {   /* [seset] を1つも書かなかった場合の保険 */
        s_n_sets = 1;
        snprintf(s_set_name[0], sizeof s_set_name[0], "%s", "標準");
        snprintf(s_set_dir[0], sizeof s_set_dir[0], "%s", "sfx");
    }
    for (int i = 0; i < s_n_battle; i++)
        if (!s_battle_name[i][0])
            snprintf(s_battle_name[i], sizeof s_battle_name[0], "曲%d", i + 1);
    return 0;
}

/* 効果音を指定セットから読み直す。
 * そのセットに音が無ければ標準セット（先頭）で補う。こうしておかないと、
 * 1つの音だけ差し替えたときに他のセットが無音になってしまう。
 * どちらにも無ければ無音（ゲームは続行できる）。 */
static void load_se_set(int set)
{
    if (set < 0 || set >= s_n_sets) set = 0;
    char path[600];
    for (int i = 0; i < SE_COUNT; i++) {
        if (s_se[i]) { Mix_FreeChunk(s_se[i]); s_se[i] = NULL; }
        if (!s_se_file[i][0]) continue;
        snprintf(path, sizeof path, "%sassets/%s/%s",
                 s_base, s_set_dir[set], s_se_file[i]);
        s_se[i] = Mix_LoadWAV(path);
        if (!s_se[i] && set != 0) {
            snprintf(path, sizeof path, "%sassets/%s/%s",
                     s_base, s_set_dir[0], s_se_file[i]);
            s_se[i] = Mix_LoadWAV(path);
        }
        if (!s_se[i]) SDL_Log("SE読込失敗: %s (%s)", path, Mix_GetError());
    }
    s_se_set = set;
}

int snd_init(const char *base_path)
{
    snprintf(s_base, sizeof s_base, "%s", base_path ? base_path : "");
    audio_defaults();
    load_audio_def(s_base);

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        SDL_Log("音声デバイスなし: %s（無音で続行）", SDL_GetError());
        return -1;
    }
    /* OGG/MP3/FLAC のデコーダを有効化（無くても WAV は鳴る） */
    Mix_Init(MIX_INIT_OGG | MIX_INIT_MP3 | MIX_INIT_FLAC);
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) != 0) {
        SDL_Log("Mix_OpenAudio 失敗: %s（無音で続行）", Mix_GetError());
        return -1;
    }
    Mix_AllocateChannels(16);

    load_se_set(0);

    char path[600];
    for (int i = 0; i < HWM_COUNT; i++) {
        if (!s_mus_file[i][0]) continue;
        snprintf(path, sizeof path, "%sassets/%s", s_base, s_mus_file[i]);
        s_mus[i] = Mix_LoadMUS(path);
        if (!s_mus[i]) SDL_Log("BGM読込失敗: %s (%s)", path, Mix_GetError());
    }
    s_enabled = true;
    return 0;
}

void snd_quit(void)
{
    if (!s_enabled) return;
    Mix_HaltMusic();
    for (int i = 0; i < SE_COUNT; i++)
        if (s_se[i]) { Mix_FreeChunk(s_se[i]); s_se[i] = NULL; }
    for (int i = 0; i < HWM_COUNT; i++)
        if (s_mus[i]) { Mix_FreeMusic(s_mus[i]); s_mus[i] = NULL; }
    Mix_CloseAudio();
    Mix_Quit();
    s_enabled = false;
}

void snd_se(int id)
{
    if (!s_enabled || id < 0 || id >= SE_COUNT || !s_se[id]) return;
    Mix_PlayChannel(-1, s_se[id], 0);
}

void snd_music(int id, bool loop)
{
    if (!s_enabled) return;
    if (id == HWM_NONE) { snd_music_stop(); return; }
    if (id < 0 || id >= HWM_COUNT || !s_mus[id]) return;
    if (s_current_mus == id && Mix_PlayingMusic()) return;
    Mix_HaltMusic();
    Mix_PlayMusic(s_mus[id], loop ? -1 : 1);
    s_current_mus = id;
}

bool snd_music_available(int id)
{
    return s_enabled && id >= 0 && id < HWM_COUNT && s_mus[id] != NULL;
}

void snd_music_stop(void)
{
    if (!s_enabled) return;
    Mix_HaltMusic();
    s_current_mus = HWM_NONE;
}

void snd_apply_volumes(int bgm, int se)
{
    if (!s_enabled) return;
    if (bgm < 0) bgm = 0;
    if (bgm > 10) bgm = 10;
    if (se < 0) se = 0;
    if (se > 10) se = 10;
    /* 人の耳は対数的なので線形だと小さい値でも大きく感じる。
     * 2乗カーブにして低い設定がちゃんと小さくなるようにする（10で最大は不変）。 */
    Mix_VolumeMusic(MIX_MAX_VOLUME * bgm * bgm / 100);
    Mix_Volume(-1, MIX_MAX_VOLUME * se * se / 100);
}

int snd_battle_track_count(void) { return s_n_battle; }

const char *snd_battle_track_name(int i)
{
    if (i < 0 || i >= s_n_battle) return "-";
    return s_battle_name[i];
}

int snd_se_set_count(void) { return s_n_sets > 0 ? s_n_sets : 1; }

const char *snd_se_set_name(int i)
{
    if (i < 0 || i >= s_n_sets) return "-";
    return s_set_name[i][0] ? s_set_name[i] : "-";
}

void snd_set_se_set(int set)
{
    if (!s_enabled) return;
    if (set < 0 || set >= s_n_sets) set = 0;
    if (set == s_se_set) return;
    load_se_set(set);
}

int snd_battle_music(int track, const char *map_name)
{
    if (s_n_battle <= 0) return HWM_NONE;
    if (track >= 0 && track < s_n_battle)
        return HWM_BATTLE0 + track;
    /* 自動: マップ名のハッシュで曲を散らす（同じマップなら毎回同じ曲） */
    unsigned h = 0;
    for (const char *c = map_name ? map_name : ""; *c; c++)
        h = h * 31u + (unsigned char)*c;
    return HWM_BATTLE0 + (int)(h % (unsigned)s_n_battle);
}
