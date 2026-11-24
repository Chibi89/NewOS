/**
 * newos/calc.h — Motore Spreadsheet & Formule per NewSoft SDK
 * Portato dalle logiche di GridCraft (CellAddr, FormulaParser, BuiltInFunctions, CalcEngine)
 */
#ifndef NEWOS_SDK_CALC_H
#define NEWOS_SDK_CALC_H

#ifndef TYPES_H
#include "types.h"
#endif

#define NS_CALC_MAX_COLS 8
#define NS_CALC_MAX_ROWS 12

static inline bool ns_cell_parse_ref(const char* s, int* out_col, int* out_row, int* out_adv) {
    if (!s || !s[0]) return false;
    char c = s[0];
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    if (c < 'A' || c >= 'A' + NS_CALC_MAX_COLS) return false;
    int col = c - 'A';
    int i = 1;
    if (s[i] < '0' || s[i] > '9') return false;
    int row = 0;
    while (s[i] >= '0' && s[i] <= '9') {
        row = row * 10 + (s[i] - '0');
        i++;
    }
    row -= 1; /* 1-based -> 0-based */
    if (row < 0 || row >= NS_CALC_MAX_ROWS) return false;
    if (out_col) *out_col = col;
    if (out_row) *out_row = row;
    if (out_adv) *out_adv = i;
    return true;
}

static inline int32_t ns_calc_atoi(const char* s, int* out_adv) {
    int i = 0;
    int32_t sign = 1;
    while (s[i] == ' ') i++;
    if (s[i] == '-') { sign = -1; i++; }
    else if (s[i] == '+') { i++; }
    int32_t val = 0;
    while (s[i] >= '0' && s[i] <= '9') {
        val = val * 10 + (s[i] - '0');
        i++;
    }
    if (out_adv) *out_adv = i;
    return val * sign;
}

static inline int32_t ns_calc_eval_cell(const char grid[NS_CALC_MAX_ROWS][NS_CALC_MAX_COLS][48],
                                         int row, int col, int depth);

static inline int32_t ns_calc_eval_atom(const char grid[NS_CALC_MAX_ROWS][NS_CALC_MAX_COLS][48],
                                        const char* expr, int* adv, int depth) {
    int i = 0;
    while (expr[i] == ' ') i++;

    /* Funzioni builtin: SUM(A1:A5), AVG(...), MIN(...), MAX(...), COUNT(...) */
    int fn = 0; /* 1=SUM, 2=AVG, 3=MIN, 4=MAX, 5=COUNT */
    if ((expr[i] == 'S' || expr[i] == 's') && (expr[i+1] == 'U' || expr[i+1] == 'u') && (expr[i+2] == 'M' || expr[i+2] == 'm') && expr[i+3] == '(') fn = 1;
    else if ((expr[i] == 'A' || expr[i] == 'a') && (expr[i+1] == 'V' || expr[i+1] == 'v') && (expr[i+2] == 'G' || expr[i+2] == 'g') && expr[i+3] == '(') fn = 2;
    else if ((expr[i] == 'M' || expr[i] == 'm') && (expr[i+1] == 'I' || expr[i+1] == 'i') && (expr[i+2] == 'N' || expr[i+2] == 'n') && expr[i+3] == '(') fn = 3;
    else if ((expr[i] == 'M' || expr[i] == 'm') && (expr[i+1] == 'A' || expr[i+1] == 'a') && (expr[i+2] == 'X' || expr[i+2] == 'x') && expr[i+3] == '(') fn = 4;

    if (fn > 0) {
        i += 4;
        int c1 = 0, r1 = 0, c2 = 0, r2 = 0, a1 = 0, a2 = 0;
        if (ns_cell_parse_ref(expr + i, &c1, &r1, &a1)) {
            i += a1;
            if (expr[i] == ':' && ns_cell_parse_ref(expr + i + 1, &c2, &r2, &a2)) {
                i += 1 + a2;
            } else {
                c2 = c1; r2 = r1;
            }
            while (expr[i] && expr[i] != ')') i++;
            if (expr[i] == ')') i++;
            if (c1 > c2) { int t = c1; c1 = c2; c2 = t; }
            if (r1 > r2) { int t = r1; r1 = r2; r2 = t; }
            int32_t sum = 0, cnt = 0, mn = 0x7FFFFFFF, mx = -0x7FFFFFFF;
            for (int r = r1; r <= r2; r++) {
                for (int c = c1; c <= c2; c++) {
                    int32_t v = ns_calc_eval_cell(grid, r, c, depth + 1);
                    sum += v;
                    cnt++;
                    if (v < mn) mn = v;
                    if (v > mx) mx = v;
                }
            }
            if (adv) *adv = i;
            if (fn == 1) return sum;
            if (fn == 2) return (cnt > 0) ? (sum / cnt) : 0;
            if (fn == 3) return (cnt > 0) ? mn : 0;
            if (fn == 4) return (cnt > 0) ? mx : 0;
        }
    }

    int rc = 0, rr = 0, ra = 0;
    if (ns_cell_parse_ref(expr + i, &rc, &rr, &ra)) {
        if (adv) *adv = i + ra;
        return ns_calc_eval_cell(grid, rr, rc, depth + 1);
    }

    int na = 0;
    int32_t num = ns_calc_atoi(expr + i, &na);
    if (adv) *adv = i + na;
    return num;
}

static inline int32_t ns_calc_eval_cell(const char grid[NS_CALC_MAX_ROWS][NS_CALC_MAX_COLS][48],
                                         int row, int col, int depth) {
    if (depth > 8 || row < 0 || row >= NS_CALC_MAX_ROWS || col < 0 || col >= NS_CALC_MAX_COLS) return 0;
    const char* raw = grid[row][col];
    if (!raw || !raw[0]) return 0;
    if (raw[0] != '=') {
        int a = 0;
        return ns_calc_atoi(raw, &a);
    }

    const char* p = raw + 1;
    int adv = 0;
    int32_t acc = ns_calc_eval_atom(grid, p, &adv, depth);
    p += adv;
    while (*p) {
        while (*p == ' ') p++;
        char op = *p;
        if (op != '+' && op != '-' && op != '*' && op != '/') break;
        p++;
        int a2 = 0;
        int32_t rhs = ns_calc_eval_atom(grid, p, &a2, depth);
        if (a2 == 0) break;
        p += a2;
        if (op == '+') acc += rhs;
        else if (op == '-') acc -= rhs;
        else if (op == '*') acc *= rhs;
        else if (op == '/' && rhs != 0) acc /= rhs;
    }
    return acc;
}

#endif /* NEWOS_SDK_CALC_H */
