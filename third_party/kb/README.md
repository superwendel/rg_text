kb_text_shape by Jimmy Lefevre, vendored unmodified.
Source: https://github.com/JimmyLefevre/kb
Commit: 7d811ad80215eef6363d60057b0ea140215278a1 (v2.28g)
License: zlib (LICENSE beside this file).

Used only by src/rg_text_shape.h, which includes it with KB_TEXT_SHAPE_STATIC and
KB_TEXT_SHAPE_IMPLEMENTATION for internal linkage. Fonts must be trusted: the
library states it provides no security guarantee on malformed font files.
