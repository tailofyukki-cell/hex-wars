/* text.h - UI文字列の外部化（data/text_ja.def。仕様書 8.4） */
#ifndef HW_TEXT_H
#define HW_TEXT_H

/* 読込。成功=0（失敗してもキーがそのまま表示されるだけで続行可能） */
int text_load(const char *path);

/* キーに対応する文字列。未定義ならキー自身を返す */
const char *tx(const char *key);

#endif
