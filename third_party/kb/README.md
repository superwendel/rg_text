kb_text_shape by Jimmy Lefevre, vendored with one local patch (below).
Source: https://github.com/JimmyLefevre/kb
Commit: 7d811ad80215eef6363d60057b0ea140215278a1 (v2.28g)
License: zlib (LICENSE beside this file).

Used only by src/rg_text_shape.h, which includes it with KB_TEXT_SHAPE_STATIC and
KB_TEXT_SHAPE_IMPLEMENTATION for internal linkage. Fonts must be trusted: the
library states it provides no security guarantee on malformed font files.

Local patch: grapheme break table (kbts_GraphemeBreakTransition), seven cells
--------------------------------------------------------------------------
Upstream v2.28g and v2.29 (abe50a2) have the same table. Two kinds of cell lose state:

- Row 16 (Extended_Pictographic), column 10 (after ExtPict Extend* ZWJ): 0 -> 8.
  The cell joined the pictograph (GB11) but reset the state to START, so a second
  ZWJ in the chain broke: a family (woman ZWJ woman ZWJ girl ZWJ boy) split
  before the girl.
- Column 7 (after consonant + linker), rows 7-11 (Hangul L V LV LVT T) and
  16 (ExtPict): 16 -> 18 19 19 20 20 24. The break was right, but the state went
  to START instead of the new codepoint's own state (b1 instead of b1toX, as
  every other column has), so ka + virama + Hangul L V split L from V, and
  ka + virama + woman ZWJ girl split at the ZWJ.

`build.bat test_kb_graphemes` (part of `build.bat test`) runs kb's break iterator
over tests/data/GraphemeBreakTest-16.0.0.txt (Unicode's conformance file for the
version kb implements; all 1093 lines pass before and after the patch) and eleven
extra lines covering ZWJ chains and every patched cell, which fail before and pass
after. Against 17.0.0 kb differs on the same 9 lines before and after (rules added
in Unicode 17: Myanmar, Balinese and Khmer conjuncts, and U+2701); pass that file
as the test's argument to see them. tests/test_text_shape.c covers three of the
extra cases through rg_text_shape. Drop the patch when upstream fixes the table.
