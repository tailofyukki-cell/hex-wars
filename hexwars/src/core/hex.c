#include "hex.h"

/* odd-r: 奇数行を右に半マスずらすポイントトップ配置 */

/* 偶数行 / 奇数行の近傍テーブル: E, NE, NW, W, SW, SE */
static const int NB_EVEN[HEX_DIRS][2] = {
    { 1, 0}, { 0,-1}, {-1,-1}, {-1, 0}, {-1, 1}, { 0, 1}
};
static const int NB_ODD[HEX_DIRS][2] = {
    { 1, 0}, { 1,-1}, { 0,-1}, {-1, 0}, { 0, 1}, { 1, 1}
};

Axial hex_to_axial(int x, int y)
{
    Axial a;
    a.q = (int8_t)(x - (y - (y & 1)) / 2);
    a.r = (int8_t)y;
    return a;
}

Cell hex_to_offset(Axial a)
{
    Cell c;
    c.x = (uint8_t)(a.q + (a.r - (a.r & 1)) / 2);
    c.y = (uint8_t)a.r;
    return c;
}

int hex_distance(int x0, int y0, int x1, int y1)
{
    Axial a = hex_to_axial(x0, y0);
    Axial b = hex_to_axial(x1, y1);
    int dq = a.q - b.q;
    int dr = a.r - b.r;
    int ds = dq + dr;
    if (dq < 0) dq = -dq;
    if (dr < 0) dr = -dr;
    if (ds < 0) ds = -ds;
    return (dq + dr + ds) / 2;
}

void hex_neighbor(int x, int y, int dir, int *nx, int *ny)
{
    const int (*tbl)[2] = (y & 1) ? NB_ODD : NB_EVEN;
    *nx = x + tbl[dir][0];
    *ny = y + tbl[dir][1];
}
