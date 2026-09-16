/* text.h - UI文字列の外部化（data/text_ja.def。仕様書 8.4） */
#ifndef HW_TEXT_H
#define HW_TEXT_H

/* 読込。成功=0（失敗してもキーがそのまま表示されるだけで続行可能） */
int text_load(const char *path);
/* 読み込み済みの文言へ別言語のファイルを重ねる。
 * 書いてあるキーだけ差し替わり、無いキーは元（日本語）のまま残る */
int text_load_overlay(const char *path);

/* キーに対応する文字列。未定義ならキー自身を返す */
const char *tx(const char *key);

#endif
