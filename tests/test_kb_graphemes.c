// kb grapheme breaks against Unicode's conformance data: every line of
// GraphemeBreakTest.txt (UCD 16.0.0, the version kb 2.28g implements), plus
// lines for emoji ZWJ chains and for each table cell patched in third_party/kb
// (see its README.md), which the official file does not cover.
//
// Usage:
//   test_kb_graphemes.exe [GraphemeBreakTest.txt]

#define RGINLINE static inline
#include "rg_defs.h"
#include "rg_mem.h"
#include "rg_text_shape.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CODEPOINTS 64

/* The file's notation, with "/" for a break (the file's division sign) and "x"
 * for none (its multiplication sign). */
static const char* const extra_lines[] = {
    "/ 1F469 x 200D x 1F469 x 200D x 1F467 x 200D x 1F466 /", /* family: one grapheme (GB11 chain) */
    "/ 1F3F3 x FE0F x 200D x 1F308 /",                         /* rainbow flag: Extend before the ZWJ */
    "/ 1F468 x 1F3FB x 200D x 1F469 x 1F3FC x 200D x 1F466 /", /* skin tones inside a chain */
    "/ 0915 x 094D / 1F469 x 200D x 1F467 /",                  /* emoji ZWJ sequence after ka + virama */
    "/ 0915 x 094D / 1100 x 1161 /",                           /* Hangul L V after ka + virama */
    "/ 0915 x 094D / 1100 x 1100 /",                           /* L L */
    "/ 0915 x 094D / 1161 x 11A8 /",                           /* V T */
    "/ 0915 x 094D / AC00 x 11A8 /",                           /* LV T */
    "/ 0915 x 094D / AC01 x 11A8 /",                           /* LVT T */
    "/ 0915 x 094D / 11A8 x 11A8 /",                           /* T T */
    "/ 0915 x 094D x 0915 /",                                  /* GB9c still joins the conjunct */
};

/* Codepoints of one test line and, per position i, whether a break is expected
 * before codepoint i. Returns the codepoint count, or -1 when the line is malformed. */
static int parse_line(const char* line, u32* cps, u8* expect)
{
	int n = 0;
	const char* p = line;
	while (*p && *p != '#' && *p != '\n' && *p != '\r')
	{
		if (*p == ' ' || *p == '\t')
		{
			p++;
		}
		else if ((u8)p[0] == 0xC3u && (u8)p[1] == 0xB7u)
		{
			expect[n] = 1u;
			p += 2;
		}
		else if ((u8)p[0] == 0xC3u && (u8)p[1] == 0x97u)
		{
			expect[n] = 0u;
			p += 2;
		}
		else if (*p == '/' || *p == 'x')
		{
			expect[n] = *p == '/' ? 1u : 0u;
			p++;
		}
		else
		{
			char* end;
			unsigned long value = strtoul(p, &end, 16);
			if (end == p || n >= MAX_CODEPOINTS) return -1;
			cps[n++] = (u32)value;
			p = end;
		}
	}
	return n;
}

static void print_breaks(const char* label, const u32* cps, const u8* breaks, int n)
{
	int i;
	printf("    %s:", label);
	for (i = 0; i < n; i++) printf(" %s%04X", i == 0 ? "" : (breaks[i] ? "/ " : "x "), (unsigned)cps[i]);
	printf("\n");
}

/* Feeds the line to kb's break iterator; 1 when its grapheme breaks match. */
static int check_line(const u32* cps, const u8* expect, int n, int report)
{
	kbts_break_state state;
	kbts_break brk;
	u8 got[MAX_CODEPOINTS + 1];
	int i, ok = 1;
	memset(got, 0, sizeof got);
	kbts_BreakBegin(&state, KBTS_DIRECTION_DONT_KNOW, KBTS_JAPANESE_LINE_BREAK_STYLE_NORMAL, 0);
	for (i = 0; i < n; i++)
	{
		kbts_BreakAddCodepoint(&state, (int)cps[i], 1, i == n - 1);
		while (kbts_Break(&state, &brk))
			if ((brk.Flags & KBTS_BREAK_FLAG_GRAPHEME) && brk.Position >= 0 && brk.Position <= n) got[brk.Position] = 1u;
	}
	for (i = 1; i < n; i++)
		if (got[i] != expect[i]) ok = 0;
	if (!ok && report)
	{
		printf("  FAIL:\n");
		print_breaks("expected", cps, expect, n);
		print_breaks("kb      ", cps, got, n);
	}
	return ok;
}

int main(int argc, char** argv)
{
	const char* path = argc > 1 ? argv[1] : "tests/data/GraphemeBreakTest-16.0.0.txt";
	static char line[8192];
	u32 cps[MAX_CODEPOINTS];
	u8 expect[MAX_CODEPOINTS + 1];
	int lines = 0, failed = 0, extra_failed = 0;
	size_t e;
	FILE* file = fopen(path, "rb");
	printf("kb grapheme breaks vs %s\n", path);
	if (!file)
	{
		printf("  FAIL: cannot open %s\n", path);
		return 1;
	}
	while (fgets(line, sizeof line, file))
	{
		int n;
		if ((u8)line[0] != 0xC3u) continue; /* comments and blank lines */
		n = parse_line(line, cps, expect);
		lines++;
		if (n <= 0 || !check_line(cps, expect, n, failed < 10)) failed++;
	}
	fclose(file);
	for (e = 0; e < sizeof extra_lines / sizeof extra_lines[0]; e++)
	{
		int n = parse_line(extra_lines[e], cps, expect);
		if (n <= 0 || !check_line(cps, expect, n, 1)) extra_failed++;
	}
	printf("  %d of %d conformance lines match, %d of %d extra lines\n", lines - failed, lines,
	       (int)(sizeof extra_lines / sizeof extra_lines[0]) - extra_failed, (int)(sizeof extra_lines / sizeof extra_lines[0]));
	return (failed || extra_failed || lines < 1000) ? 1 : 0;
}
