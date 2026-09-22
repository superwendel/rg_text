// rg_text_bake - Bake a TTF font into an RGBA8 atlas + RGFONT metrics
//
// Usage:
//   rg_text_bake.exe [--no-kerning] input.ttf output_base [pixel_size] [first_codepoint] [last_codepoint] [padding] [atlas_width]
//   rg_text_bake.exe [--no-kerning] input.ttf output_base [pixel_size] [range_list] [padding] [atlas_width]
//
// Example:
//   rg_text_bake.exe font/chrono.ttf font/chrono_16 16
//   rg_text_bake.exe font/chrono.ttf font/chrono_cyrillic_16 16 32-126,1024-1279

#include "rg_defs.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define RG_TEXT_BAKE_SHADOW_OFFSET_X 1
#define RG_TEXT_BAKE_SHADOW_OFFSET_Y 1
#define RG_TEXT_BAKE_SHADOW_R 5
#define RG_TEXT_BAKE_SHADOW_G 5
#define RG_TEXT_BAKE_SHADOW_B 8
#define RG_TEXT_BAKE_MAX_ATLAS_DIM 16384u
#define RG_TEXT_BAKE_MAX_CODEPOINTS 2048u
#define RG_TEXT_BAKE_MAX_KERNING_GLYPHS 512u
#define RG_TEXT_BAKE_MAX_KERNINGS 65536u
#define RG_TEXT_BAKE_MAX_CODEPOINT 0x10FFFFu
#define RG_TEXT_BAKE_SELECTION_BYTES ((RG_TEXT_BAKE_MAX_CODEPOINT + 8u) / 8u)
#define RG_TEXT_BAKE_FT_RENDER_FLAGS \
	(FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL | FT_LOAD_NO_BITMAP)
#define RG_TEXT_BAKE_FT_SHAPE_FLAGS \
	(FT_LOAD_DEFAULT | FT_LOAD_TARGET_NORMAL | FT_LOAD_NO_BITMAP)

typedef struct BakedGlyph
{
	u32 codepoint;
	FT_UInt glyph_index;
	int x;
	int y;
	int w;
	int h;
	int bitmap_w;
	int bitmap_h;
	int x_offset;
	int y_offset;
	int x_advance;
} BakedGlyph;

typedef struct BakeRange
{
	u32 first;
	u32 last;
} BakeRange;

typedef struct BakedKerning
{
	u32 left;
	u32 right;
	int x_advance;
} BakedKerning;

typedef struct BakeLock
{
	char path[1024];
#if defined(_WIN32)
	HANDLE handle;
#else
	int descriptor;
#endif
	int held;
} BakeLock;

static int acquire_bake_lock(BakeLock* lock, const char* output_base)
{
	memset(lock, 0, sizeof(*lock));
	int written = snprintf(lock->path, sizeof(lock->path), "%s.rg_text_bake.lock",
	                       output_base);
	if (written < 0 || (size_t)written >= sizeof(lock->path))
	{
		return 0;
	}

#if defined(_WIN32)
	lock->handle = CreateFileA(lock->path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
	                           OPEN_ALWAYS,
	                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
	                           NULL);
	if (lock->handle == INVALID_HANDLE_VALUE)
	{
		return 0;
	}
#else
	lock->descriptor = -1;
	for (int attempt = 0; attempt < 3; attempt++)
	{
		int descriptor = open(lock->path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
		if (descriptor < 0)
		{
			return 0;
		}
		if (flock(descriptor, LOCK_EX | LOCK_NB) != 0)
		{
			close(descriptor);
			return 0;
		}

		/*
		 * The previous owner unlinks before unlock. If we opened that old
		 * inode during release, retry on the current pathname so a third
		 * process cannot lock a different inode for the same output base.
		 */
		struct stat descriptor_status;
		struct stat path_status;
		if (fstat(descriptor, &descriptor_status) == 0 &&
		    stat(lock->path, &path_status) == 0 &&
		    descriptor_status.st_dev == path_status.st_dev &&
		    descriptor_status.st_ino == path_status.st_ino)
		{
			lock->descriptor = descriptor;
			break;
		}
		flock(descriptor, LOCK_UN);
		close(descriptor);
	}
	if (lock->descriptor < 0)
	{
		return 0;
	}
#endif

	lock->held = 1;
	return 1;
}

static void release_bake_lock(BakeLock* lock)
{
	if (!lock->held)
	{
		return;
	}

#if defined(_WIN32)
	CloseHandle(lock->handle);
#else
	unlink(lock->path);
	flock(lock->descriptor, LOCK_UN);
	close(lock->descriptor);
#endif
	lock->held = 0;
}

static int write_file(FILE* file, const void* data, size_t size)
{
	if (!file)
	{
		return 0;
	}

	size_t written = fwrite(data, 1, size, file);
	return written == size && ferror(file) == 0 && fflush(file) == 0;
}

static unsigned long process_id(void)
{
#if defined(_WIN32)
	return (unsigned long)GetCurrentProcessId();
#else
	return (unsigned long)getpid();
#endif
}

static int reserve_work_file(char* dst,
                             size_t dst_size,
                             const char* base,
                             const char* kind,
                             const char* purpose,
                             FILE** out_file)
{
	*out_file = NULL;
	for (u32 attempt = 0u; attempt < 1000u; attempt++)
	{
		int written = snprintf(dst, dst_size, "%s.%s.%s.%lu.%u", base, kind,
		                       purpose, process_id(), attempt);
		if (written < 0 || (size_t)written >= dst_size)
		{
			return 0;
		}

		errno = 0;
		FILE* file = fopen(dst, "wbx");
		if (file)
		{
			*out_file = file;
			return 1;
		}
		if (errno != EEXIST)
		{
			return 0;
		}
	}

	return 0;
}

static int replace_file(const char* staged_path, const char* final_path)
{
#if defined(_WIN32)
	return MoveFileExA(staged_path, final_path,
	                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
	return rename(staged_path, final_path) == 0;
#endif
}

static int close_work_file(FILE** file)
{
	if (!*file)
	{
		return 1;
	}
	int result = fclose(*file);
	*file = NULL;
	return result == 0;
}

/* Copies an existing file for rollback. A missing source is also success. */
static int backup_file_if_present(const char* source_path,
                                  FILE* backup,
                                  int* out_present)
{
	*out_present = 0;
	errno = 0;
	FILE* source = fopen(source_path, "rb");
	if (!source)
	{
		return errno == ENOENT;
	}

	if (!backup)
	{
		fclose(source);
		return 0;
	}

	u8 buffer[64u * 1024u];
	int success = 1;
	for (;;)
	{
		size_t read_size = fread(buffer, 1, sizeof(buffer), source);
		if (read_size != 0u && fwrite(buffer, 1, read_size, backup) != read_size)
		{
			success = 0;
			break;
		}
		if (read_size != sizeof(buffer))
		{
			if (ferror(source))
			{
				success = 0;
			}
			break;
		}
	}

	int source_close_result = fclose(source);
	if (source_close_result != 0 || ferror(backup) || fflush(backup) != 0)
	{
		success = 0;
	}
	if (!success)
	{
		return 0;
	}

	*out_present = 1;
	return 1;
}

static u32 next_pow2_u32(u32 value)
{
	if (value <= 1u)
	{
		return 1u;
	}

	value--;
	value |= value >> 1u;
	value |= value >> 2u;
	value |= value >> 4u;
	value |= value >> 8u;
	value |= value >> 16u;
	return value + 1u;
}

static int round_symmetric(double value, int* out_value)
{
	if (!out_value || value != value)
	{
		return 0;
	}

	double rounded = value >= 0.0 ? value + 0.5 : value - 0.5;
	if (rounded <= (double)INT_MIN - 1.0 || rounded >= (double)INT_MAX + 1.0)
	{
		return 0;
	}

	*out_value = (int)rounded;
	return 1;
}

static int round_26_6(i64 value, int* out_value)
{
	if (!out_value)
	{
		return 0;
	}

	i64 rounded = value / 64;
	i64 remainder = value % 64;
	if (remainder >= 32)
	{
		rounded++;
	}
	else if (remainder <= -32)
	{
		rounded--;
	}
	if (rounded < INT_MIN || rounded > INT_MAX)
	{
		return 0;
	}

	*out_value = (int)rounded;
	return 1;
}

static int parse_u32_arg(const char* text, u32* out_value)
{
	if (!text || *text == '\0')
	{
		return 0;
	}

	int force_hex = 0;
	if (text[0] == 'U' && text[1] == '+')
	{
		text += 2;
		force_hex = 1;
	}
	if (*text == '\0' || *text == '-' || *text == '+')
	{
		return 0;
	}

	errno = 0;
	char* end = NULL;
	unsigned long long value = strtoull(text, &end, force_hex ? 16 : 0);
	if (!end || *end != '\0' || errno == ERANGE ||
	    value > 0xFFFFFFFFull)
	{
		return 0;
	}

	*out_value = (u32)value;
	return 1;
}

static int parse_range_token(char* token, BakeRange* out_range)
{
	char* dash = strchr(token, '-');
	if (dash)
	{
		*dash = '\0';
		dash++;
		if (!parse_u32_arg(token, &out_range->first) ||
		    !parse_u32_arg(dash, &out_range->last) ||
		    out_range->first > out_range->last)
		{
			return 0;
		}
		return 1;
	}

	if (!parse_u32_arg(token, &out_range->first))
	{
		return 0;
	}
	out_range->last = out_range->first;
	return 1;
}

static int parse_range_list(const char* text, BakeRange* ranges, u32 range_capacity, u32* out_range_count)
{
	if (!text || *text == '\0')
	{
		return 0;
	}

	size_t len = strlen(text);
	char* copy = (char*)malloc(len + 1u);
	if (!copy)
	{
		return 0;
	}

	memcpy(copy, text, len + 1u);

	u32 count = 0u;
	char* token = copy;
	for (;;)
	{
		char* comma = strchr(token, ',');
		if (comma)
		{
			*comma = '\0';
		}

		if (*token == '\0' || count >= range_capacity ||
		    !parse_range_token(token, &ranges[count]))
		{
			free(copy);
			return 0;
		}
		count++;

		if (!comma)
		{
			break;
		}
		token = comma + 1;
	}

	free(copy);
	if (count == 0u)
	{
		return 0;
	}

	*out_range_count = count;
	return 1;
}

static int range_arg_is_list(const char* text)
{
	return strchr(text, '-') != NULL || strchr(text, ',') != NULL || strstr(text, "U+") == text;
}

static int make_output_path(char* dst, size_t dst_size, const char* base, const char* ext)
{
	int written = snprintf(dst, dst_size, "%s%s", base, ext);
	return written >= 0 && (size_t)written < dst_size;
}

static int codepoint_is_scalar(u32 codepoint)
{
	return codepoint <= RG_TEXT_BAKE_MAX_CODEPOINT &&
	       (codepoint < 0xD800u || codepoint > 0xDFFFu);
}

static int selection_contains(const u8* selection, u32 codepoint)
{
	return (selection[codepoint >> 3u] & (u8)(1u << (codepoint & 7u))) != 0u;
}

static void selection_add(u8* selection, u32 codepoint)
{
	selection[codepoint >> 3u] |= (u8)(1u << (codepoint & 7u));
}

/* Returns 1 on success, 0 for an invalid/empty selection, and -1 at the cap. */
static int build_selection(const BakeRange* ranges,
                           u32 range_count,
                           u8* selection,
                           u32* out_count)
{
	u32 count = 0u;
	memset(selection, 0, RG_TEXT_BAKE_SELECTION_BYTES);

	for (u32 range_index = 0u; range_index < range_count; range_index++)
	{
		u32 first = ranges[range_index].first;
		u32 last = ranges[range_index].last;
		if (first > last || last > RG_TEXT_BAKE_MAX_CODEPOINT)
		{
			return 0;
		}

		for (u32 codepoint = first;; codepoint++)
		{
			if (codepoint_is_scalar(codepoint) && !selection_contains(selection, codepoint))
			{
				if (count >= RG_TEXT_BAKE_MAX_CODEPOINTS)
				{
					return -1;
				}
				selection_add(selection, codepoint);
				count++;
			}

			if (codepoint == last)
			{
				break;
			}
		}
	}

	*out_count = count;
	return count != 0u;
}

static int scan_glyphs(FT_Face face,
                       const u8* selection,
                       int padding,
                       u32 atlas_width,
                       BakedGlyph* glyphs,
                       u32* out_glyph_count,
                       u32* out_atlas_height,
                       int ascent)
{
	u32 glyph_count = 0u;
	int x = padding;
	int y = padding;
	int row_h = 0;

	for (u32 codepoint = 0u; codepoint <= RG_TEXT_BAKE_MAX_CODEPOINT; codepoint++)
	{
		if (!selection_contains(selection, codepoint))
		{
			continue;
		}

		FT_UInt glyph_index = FT_Get_Char_Index(face, (FT_ULong)codepoint);
		if (glyph_index == 0u)
		{
			continue;
		}

		FT_Error load_error = FT_Load_Glyph(face, glyph_index, RG_TEXT_BAKE_FT_RENDER_FLAGS);
		if (load_error != 0)
		{
			fprintf(stderr, "FreeType failed to render U+%04X (error %d).\n",
			        codepoint, (int)load_error);
			return 0;
		}

		FT_GlyphSlot slot = face->glyph;
		const FT_Bitmap* bitmap = &slot->bitmap;
		if (bitmap->width > (unsigned int)(INT_MAX - RG_TEXT_BAKE_SHADOW_OFFSET_X) ||
		    bitmap->rows > (unsigned int)(INT_MAX - RG_TEXT_BAKE_SHADOW_OFFSET_Y))
		{
			return 0;
		}

		int bitmap_w = (int)bitmap->width;
		int bitmap_h = (int)bitmap->rows;
		int has_bitmap = bitmap_w > 0 && bitmap_h > 0;
		if (has_bitmap &&
		    (bitmap->pixel_mode != FT_PIXEL_MODE_GRAY || bitmap->num_grays < 2u ||
		     !bitmap->buffer))
		{
			fprintf(stderr, "FreeType returned an unsupported bitmap for U+%04X.\n",
			        codepoint);
			return 0;
		}

		int w = has_bitmap ? bitmap_w + RG_TEXT_BAKE_SHADOW_OFFSET_X : 0;
		int h = has_bitmap ? bitmap_h + RG_TEXT_BAKE_SHADOW_OFFSET_Y : 0;
		if (w > 0 && (u32)w + (u32)padding * 2u > atlas_width)
		{
			return 0;
		}

		if (w > 0 && x + w + padding > (int)atlas_width)
		{
			if (row_h > (int)RG_TEXT_BAKE_MAX_ATLAS_DIM - y - padding)
			{
				return 0;
			}
			x = padding;
			y += row_h + padding;
			row_h = 0;
		}

		i64 y_offset64 = (i64)ascent - (i64)slot->bitmap_top;
		if (y_offset64 < INT_MIN || y_offset64 > INT_MAX)
		{
			return 0;
		}

		BakedGlyph* glyph = &glyphs[glyph_count++];
		glyph->codepoint = codepoint;
		glyph->glyph_index = glyph_index;
		glyph->x = w > 0 ? x : 0;
		glyph->y = h > 0 ? y : 0;
		glyph->w = w;
		glyph->h = h;
		glyph->bitmap_w = bitmap_w;
		glyph->bitmap_h = bitmap_h;
		glyph->x_offset = slot->bitmap_left;
		glyph->y_offset = (int)y_offset64;
		if (!round_26_6((i64)slot->advance.x, &glyph->x_advance))
		{
			fprintf(stderr, "FreeType returned an out-of-range advance for U+%04X.\n",
			        codepoint);
			return 0;
		}

		if (w > 0)
		{
			x += w + padding;
			if (h > row_h)
			{
				row_h = h;
			}
		}
	}

	if (glyph_count == 0u)
	{
		return 0;
	}

	if (row_h > (int)RG_TEXT_BAKE_MAX_ATLAS_DIM - y - padding)
	{
		return 0;
	}
	u32 used_height = (u32)(y + row_h + padding);
	*out_glyph_count = glyph_count;
	*out_atlas_height = next_pow2_u32(used_height);
	return *out_atlas_height != 0u && *out_atlas_height <= RG_TEXT_BAKE_MAX_ATLAS_DIM;
}

static void blit_shadow_layer(const u8* alpha,
                              int bitmap_w,
                              int bitmap_h,
                              int dst_x,
                              int dst_y,
                              u8* pixels,
                              u32 atlas_width)
{
	for (int row = 0; row < bitmap_h; row++)
	{
		const u8* src = alpha + (size_t)row * (size_t)bitmap_w;
		u8* dst = pixels + (((size_t)(dst_y + row) * (size_t)atlas_width +
		                     (size_t)dst_x) *
		                    4u);
		for (int col = 0; col < bitmap_w; col++)
		{
			u8 value = src[col];
			if (value != 0u)
			{
				u8* px = dst + (size_t)col * 4u;
				if (value > px[3])
				{
					px[0] = RG_TEXT_BAKE_SHADOW_R;
					px[1] = RG_TEXT_BAKE_SHADOW_G;
					px[2] = RG_TEXT_BAKE_SHADOW_B;
					px[3] = value;
				}
			}
		}
	}
}

static void blit_foreground_layer(const u8* alpha,
                                  int bitmap_w,
                                  int bitmap_h,
                                  int dst_x,
                                  int dst_y,
                                  u8* pixels,
                                  u32 atlas_width)
{
	for (int row = 0; row < bitmap_h; row++)
	{
		const u8* src = alpha + (size_t)row * (size_t)bitmap_w;
		u8* dst = pixels + (((size_t)(dst_y + row) * (size_t)atlas_width +
		                     (size_t)dst_x) *
		                    4u);
		for (int col = 0; col < bitmap_w; col++)
		{
			u8 value = src[col];
			if (value != 0u)
			{
				u8* px = dst + (size_t)col * 4u;
				/* Keep the atlas straight-alpha while compositing over its shadow. */
				u32 foreground = (u32)value * 255u;
				u32 background = (u32)px[3] * (255u - (u32)value);
				u32 combined = foreground + background;
				for (u32 channel = 0u; channel < 3u; channel++)
				{
					px[channel] = (u8)((255u * foreground + (u32)px[channel] * background +
					                     combined / 2u) /
					                    combined);
				}
				px[3] = (u8)((combined + 127u) / 255u);
			}
		}
	}
}

static int copy_bitmap_alpha(const FT_Bitmap* bitmap,
                             int bitmap_w,
                             int bitmap_h,
                             u8* alpha)
{
	if (!bitmap || !alpha || !bitmap->buffer || bitmap_w <= 0 || bitmap_h <= 0 ||
	    bitmap->pixel_mode != FT_PIXEL_MODE_GRAY || bitmap->num_grays < 2u)
	{
		return 0;
	}

	i64 pitch = (i64)bitmap->pitch;
	u64 row_stride = (u64)(pitch >= 0 ? pitch : -pitch);
	if (row_stride < (u64)bitmap_w)
	{
		return 0;
	}
	u64 last_row_offset = (u64)(bitmap_h - 1) * row_stride;
	if (last_row_offset > (u64)SIZE_MAX ||
	    (u64)bitmap_w > (u64)SIZE_MAX - last_row_offset)
	{
		return 0;
	}

	for (int row = 0; row < bitmap_h; row++)
	{
		u64 source_row = pitch >= 0 ? (u64)row : (u64)(bitmap_h - 1 - row);
		const u8* src = bitmap->buffer + (size_t)(source_row * row_stride);
		u8* dst = alpha + (size_t)row * (size_t)bitmap_w;
		for (int col = 0; col < bitmap_w; col++)
		{
			unsigned int value = src[col];
			if (bitmap->num_grays != 256u)
			{
				unsigned int maximum = (unsigned int)bitmap->num_grays - 1u;
				value = (value * 255u + maximum / 2u) / maximum;
			}
			dst[col] = (u8)value;
		}
	}

	return 1;
}

static int blit_glyphs(FT_Face face,
                       const BakedGlyph* glyphs,
                       u32 glyph_count,
                       u8* pixels,
                       u32 atlas_width)
{
	size_t max_bitmap_bytes = 0u;
	for (u32 i = 0u; i < glyph_count; i++)
	{
		const BakedGlyph* glyph = &glyphs[i];
		if (glyph->bitmap_w <= 0 || glyph->bitmap_h <= 0)
		{
			continue;
		}

		if ((size_t)glyph->bitmap_w > SIZE_MAX / (size_t)glyph->bitmap_h)
		{
			return 0;
		}
		size_t bitmap_bytes = (size_t)glyph->bitmap_w * (size_t)glyph->bitmap_h;
		if (bitmap_bytes > max_bitmap_bytes)
		{
			max_bitmap_bytes = bitmap_bytes;
		}
	}

	u8* alpha = max_bitmap_bytes != 0u ? (u8*)malloc(max_bitmap_bytes) : NULL;
	if (max_bitmap_bytes != 0u && !alpha)
	{
		return 0;
	}

	for (u32 i = 0u; i < glyph_count; i++)
	{
		const BakedGlyph* glyph = &glyphs[i];
		if (glyph->bitmap_w <= 0 || glyph->bitmap_h <= 0)
		{
			continue;
		}

		FT_Error load_error =
		    FT_Load_Glyph(face, glyph->glyph_index, RG_TEXT_BAKE_FT_RENDER_FLAGS);
		if (load_error != 0)
		{
			fprintf(stderr, "FreeType failed to rerender U+%04X (error %d).\n",
			        glyph->codepoint, (int)load_error);
			free(alpha);
			return 0;
		}

		const FT_Bitmap* bitmap = &face->glyph->bitmap;
		if ((int)bitmap->width != glyph->bitmap_w ||
		    (int)bitmap->rows != glyph->bitmap_h ||
		    bitmap->pixel_mode != FT_PIXEL_MODE_GRAY || bitmap->num_grays < 2u ||
		    !bitmap->buffer)
		{
			fprintf(stderr, "FreeType returned inconsistent bitmap data for U+%04X.\n",
			        glyph->codepoint);
			free(alpha);
			return 0;
		}

		if (!copy_bitmap_alpha(bitmap, glyph->bitmap_w, glyph->bitmap_h, alpha))
		{
			fprintf(stderr, "FreeType returned an invalid bitmap pitch for U+%04X.\n",
			        glyph->codepoint);
			free(alpha);
			return 0;
		}

		blit_shadow_layer(alpha, glyph->bitmap_w, glyph->bitmap_h,
		                  glyph->x + RG_TEXT_BAKE_SHADOW_OFFSET_X, glyph->y,
		                  pixels, atlas_width);
		blit_shadow_layer(alpha, glyph->bitmap_w, glyph->bitmap_h,
		                  glyph->x + RG_TEXT_BAKE_SHADOW_OFFSET_X,
		                  glyph->y + RG_TEXT_BAKE_SHADOW_OFFSET_Y,
		                  pixels, atlas_width);
		blit_foreground_layer(alpha, glyph->bitmap_w, glyph->bitmap_h,
		                      glyph->x, glyph->y, pixels, atlas_width);
	}

	free(alpha);
	return 1;
}

static u32 choose_fallback_codepoint(const BakedGlyph* glyphs, u32 glyph_count)
{
	static const u32 preferred[] = {0x003Fu, 0xFFFDu};
	for (u32 preferred_index = 0u;
	     preferred_index < (u32)RG_ARRAY_COUNT(preferred);
	     preferred_index++)
	{
		for (u32 glyph_index = 0u; glyph_index < glyph_count; glyph_index++)
		{
			if (glyphs[glyph_index].codepoint == preferred[preferred_index])
			{
				return preferred[preferred_index];
			}
		}
	}

	for (u32 glyph_index = 0u; glyph_index < glyph_count; glyph_index++)
	{
		if (glyphs[glyph_index].bitmap_w > 0 && glyphs[glyph_index].bitmap_h > 0)
		{
			return glyphs[glyph_index].codepoint;
		}
	}

	return glyphs[0].codepoint;
}

static int kerning_work_is_bounded(u32 glyph_count, int include_kerning)
{
	return !include_kerning || glyph_count <= RG_TEXT_BAKE_MAX_KERNING_GLYPHS;
}

typedef struct ShapedPair
{
	hb_codepoint_t glyphs[2];
	u32 clusters[2];
	hb_position_t x_advances[2];
	hb_position_t y_advances[2];
	hb_position_t x_offsets[2];
	hb_position_t y_offsets[2];
} ShapedPair;

/*
 * Shape an isolated, non-boundary left-to-right pair while disabling
 * substitutions that the codepoint-keyed RGFONT format cannot represent. A
 * zero result means the pair was valid but did not remain two nominal glyphs;
 * a negative result is a HarfBuzz allocation failure and aborts the bake.
 */
static int shape_pair(hb_font_t* font,
                      hb_buffer_t* buffer,
                      u32 left,
                      u32 right,
                      int enable_kerning,
                      ShapedPair* out_pair)
{
	static const hb_tag_t disabled_tags[] = {
	    HB_TAG('l', 'i', 'g', 'a'),
	    HB_TAG('c', 'l', 'i', 'g'),
	    HB_TAG('d', 'l', 'i', 'g'),
	    HB_TAG('h', 'l', 'i', 'g'),
	    HB_TAG('r', 'l', 'i', 'g'),
	    HB_TAG('c', 'a', 'l', 't'),
	    HB_TAG('r', 'c', 'l', 't'),
	};
	hb_feature_t features[1u + RG_ARRAY_COUNT(disabled_tags)];
	features[0].tag = HB_TAG('k', 'e', 'r', 'n');
	features[0].value = enable_kerning ? 1u : 0u;
	features[0].start = HB_FEATURE_GLOBAL_START;
	features[0].end = HB_FEATURE_GLOBAL_END;
	for (u32 i = 0u; i < (u32)(sizeof(disabled_tags) / sizeof(disabled_tags[0])); i++)
	{
		features[i + 1u].tag = disabled_tags[i];
		features[i + 1u].value = 0u;
		features[i + 1u].start = HB_FEATURE_GLOBAL_START;
		features[i + 1u].end = HB_FEATURE_GLOBAL_END;
	}

	hb_buffer_reset(buffer);
	hb_buffer_set_content_type(buffer, HB_BUFFER_CONTENT_TYPE_UNICODE);
	hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
	hb_buffer_set_language(buffer, hb_language_from_string("und", -1));
	hb_buffer_set_cluster_level(buffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);
	hb_buffer_add(buffer, (hb_codepoint_t)left, 0u);
	hb_buffer_add(buffer, (hb_codepoint_t)right, 1u);
	hb_buffer_guess_segment_properties(buffer);
	if (!hb_buffer_allocation_successful(buffer))
	{
		return -1;
	}

	hb_shape(font, buffer, features, (unsigned int)(sizeof(features) / sizeof(features[0])));
	if (!hb_buffer_allocation_successful(buffer))
	{
		return -1;
	}

	unsigned int info_count = 0u;
	unsigned int position_count = 0u;
	hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &info_count);
	hb_glyph_position_t* positions =
	    hb_buffer_get_glyph_positions(buffer, &position_count);
	if (info_count != 2u || position_count != 2u)
	{
		return 0;
	}
	if (!infos || !positions)
	{
		return -1;
	}

	for (u32 i = 0u; i < 2u; i++)
	{
		out_pair->glyphs[i] = infos[i].codepoint;
		out_pair->clusters[i] = infos[i].cluster;
		out_pair->x_advances[i] = positions[i].x_advance;
		out_pair->y_advances[i] = positions[i].y_advance;
		out_pair->x_offsets[i] = positions[i].x_offset;
		out_pair->y_offsets[i] = positions[i].y_offset;
	}
	return 1;
}

/* Returns 1 for a representable pair, 0 to omit it, and -1 on failure. */
static int get_pair_kerning(hb_font_t* font,
                            hb_buffer_t* buffer,
                            const BakedGlyph* left,
                            const BakedGlyph* right,
                            int* out_kerning)
{
	ShapedPair without_kerning;
	ShapedPair with_kerning;
	int without_result = shape_pair(font, buffer, left->codepoint, right->codepoint,
	                                0, &without_kerning);
	if (without_result <= 0)
	{
		return without_result;
	}
	int with_result = shape_pair(font, buffer, left->codepoint, right->codepoint,
	                             1, &with_kerning);
	if (with_result <= 0)
	{
		return with_result;
	}

	for (u32 i = 0u; i < 2u; i++)
	{
		hb_codepoint_t expected =
		    i == 0u ? (hb_codepoint_t)left->glyph_index : (hb_codepoint_t)right->glyph_index;
		if (without_kerning.glyphs[i] != expected || with_kerning.glyphs[i] != expected ||
		    without_kerning.clusters[i] != i || with_kerning.clusters[i] != i ||
		    without_kerning.x_offsets[i] != with_kerning.x_offsets[i] ||
		    without_kerning.y_offsets[i] != with_kerning.y_offsets[i] ||
		    without_kerning.y_advances[i] != with_kerning.y_advances[i])
		{
			return 0;
		}
	}

	/*
	 * RGFONT inserts one horizontal adjustment after the left glyph. It can
	 * exactly represent a kern change to that advance, but not placement or a
	 * change to the right glyph's own advance.
	 */
	if (without_kerning.x_advances[1] != with_kerning.x_advances[1])
	{
		return 0;
	}

	i64 delta = (i64)with_kerning.x_advances[0] -
	            (i64)without_kerning.x_advances[0];
	return round_26_6(delta, out_kerning) ? 1 : -2;
}

static int write_rgfont(FILE* file,
                        hb_font_t* font,
                        const BakedGlyph* glyphs,
                        u32 glyph_count,
                        u32 atlas_width,
                        u32 atlas_height,
                        int line_height,
                        int ascent,
                        int descent,
                        int include_kerning,
                        u32* out_kerning_count)
{
	BakedKerning* kernings = NULL;
	hb_buffer_t* buffer = NULL;
	u32 kerning_count = 0u;
	if (include_kerning)
	{
		kernings =
		    (BakedKerning*)malloc((size_t)RG_TEXT_BAKE_MAX_KERNINGS * sizeof(BakedKerning));
		if (!kernings)
		{
			return 0;
		}
		buffer = hb_buffer_create();
		if (!buffer || !hb_buffer_allocation_successful(buffer))
		{
			hb_buffer_destroy(buffer);
			free(kernings);
			return 0;
		}

		for (u32 i = 0u; i < glyph_count; i++)
		{
			for (u32 j = 0u; j < glyph_count; j++)
			{
				int kern = 0;
				int pair_result =
				    get_pair_kerning(font, buffer, &glyphs[i], &glyphs[j], &kern);
				if (pair_result < 0)
				{
					if (pair_result == -1)
					{
						fprintf(stderr, "HarfBuzz ran out of memory while shaping kerning pairs.\n");
					}
					else
					{
						fprintf(stderr, "HarfBuzz returned an out-of-range kerning adjustment.\n");
					}
					hb_buffer_destroy(buffer);
					free(kernings);
					return 0;
				}
				if (pair_result == 0 || kern == 0)
				{
					continue;
				}

				if (kerning_count >= RG_TEXT_BAKE_MAX_KERNINGS)
				{
					fprintf(stderr,
					        "Font produces more than %u nonzero kerning pairs; narrow the bake range.\n",
					        RG_TEXT_BAKE_MAX_KERNINGS);
					hb_buffer_destroy(buffer);
					free(kernings);
					return 0;
				}

				BakedKerning* output = &kernings[kerning_count++];
				output->left = glyphs[i].codepoint;
				output->right = glyphs[j].codepoint;
				output->x_advance = kern;
			}
		}
		hb_buffer_destroy(buffer);
	}

	if (!file)
	{
		free(kernings);
		return 0;
	}

	u32 fallback = choose_fallback_codepoint(glyphs, glyph_count);
	if (fprintf(file, "rgfont 1\n") < 0 ||
	    fprintf(file, "atlas %u %u\n", atlas_width, atlas_height) < 0 ||
	    fprintf(file, "line_height %d\n", line_height) < 0 ||
	    fprintf(file, "ascent %d\n", ascent) < 0 ||
	    fprintf(file, "descent %d\n", descent) < 0 ||
	    fprintf(file, "fallback %u\n", fallback) < 0)
	{
		free(kernings);
		return 0;
	}

	for (u32 i = 0u; i < glyph_count; i++)
	{
		const BakedGlyph* glyph = &glyphs[i];
		if (fprintf(file, "glyph %u %d %d %d %d %d %d %d\n",
		            glyph->codepoint,
		            glyph->x,
		            glyph->y,
		            glyph->w,
		            glyph->h,
		            glyph->x_offset,
		            glyph->y_offset,
		            glyph->x_advance) < 0)
		{
			free(kernings);
			return 0;
		}
	}

	for (u32 i = 0u; i < kerning_count; i++)
	{
		if (fprintf(file, "kerning %u %u %d\n",
		            kernings[i].left,
		            kernings[i].right,
		            kernings[i].x_advance) < 0)
		{
			free(kernings);
			return 0;
		}
	}

	int write_error = ferror(file);
	int flush_result = fflush(file);
	free(kernings);
	if (write_error || flush_result != 0)
	{
		return 0;
	}
	*out_kerning_count = kerning_count;
	return 1;
}

static int run_self_tests(void)
{
	static const struct
	{
		double value;
		int expected;
	} round_cases[] = {
	    {-1.50, -2},
	    {-1.00, -1},
	    {-0.51, -1},
	    {-0.50, -1},
	    {-0.49, 0},
	    {0.00, 0},
	    {0.49, 0},
	    {0.50, 1},
	    {0.51, 1},
	    {1.00, 1},
	    {1.50, 2},
	};

	for (u32 i = 0u; i < (u32)(sizeof(round_cases) / sizeof(round_cases[0])); i++)
	{
		int rounded = 0;
		if (!round_symmetric(round_cases[i].value, &rounded) ||
		    rounded != round_cases[i].expected)
		{
			fprintf(stderr, "symmetric rounding self-test failed for %.2f\n",
			        round_cases[i].value);
			return 0;
		}
	}
	static const struct
	{
		i64 value;
		int expected;
	} fixed_cases[] = {
	    {0, 0},
	    {1, 0},
	    {31, 0},
	    {32, 1},
	    {33, 1},
	    {63, 1},
	    {64, 1},
	    {95, 1},
	    {96, 2},
	};
	for (u32 i = 0u; i < (u32)(sizeof(fixed_cases) / sizeof(fixed_cases[0])); i++)
	{
		int positive = 0;
		int negative = 0;
		if (!round_26_6(fixed_cases[i].value, &positive) ||
		    !round_26_6(-fixed_cases[i].value, &negative) ||
		    positive != fixed_cases[i].expected ||
		    negative != -fixed_cases[i].expected)
		{
			fprintf(stderr, "26.6 rounding self-test failed for %lld\n",
			        (long long)fixed_cases[i].value);
			return 0;
		}
	}
	int boundary = 0;
	i64 maximum_fixed = (i64)INT_MAX * 64;
	i64 minimum_fixed = (i64)INT_MIN * 64;
	if (!round_26_6(maximum_fixed + 31, &boundary) || boundary != INT_MAX ||
	    round_26_6(maximum_fixed + 32, &boundary) ||
	    !round_26_6(minimum_fixed - 31, &boundary) || boundary != INT_MIN ||
	    round_26_6(minimum_fixed - 32, &boundary))
	{
		fprintf(stderr, "26.6 boundary self-test failed\n");
		return 0;
	}
	if (!round_symmetric((double)INT_MAX, &boundary) || boundary != INT_MAX ||
	    !round_symmetric((double)INT_MIN, &boundary) || boundary != INT_MIN ||
	    round_symmetric((double)INT_MAX * 2.0, &boundary) ||
	    round_symmetric((double)INT_MIN * 2.0, &boundary))
	{
		fprintf(stderr, "floating-point rounding boundary self-test failed\n");
		return 0;
	}
	if (!kerning_work_is_bounded(RG_TEXT_BAKE_MAX_KERNING_GLYPHS, 1) ||
	    kerning_work_is_bounded(RG_TEXT_BAKE_MAX_KERNING_GLYPHS + 1u, 1) ||
	    !kerning_work_is_bounded(RG_TEXT_BAKE_MAX_CODEPOINTS, 0))
	{
		fprintf(stderr, "kerning work cap self-test failed\n");
		return 0;
	}

	static const struct
	{
		u8 foreground_alpha;
		u8 shadow_alpha;
		u8 expected[4];
	} composite_cases[] = {
	    {0u, 128u, {5u, 5u, 8u, 128u}},
	    {1u, 255u, {6u, 6u, 9u, 255u}},
	    {128u, 128u, {172u, 172u, 173u, 192u}},
	    {64u, 128u, {105u, 105u, 107u, 160u}},
	    {128u, 255u, {130u, 130u, 132u, 255u}},
	    {128u, 0u, {255u, 255u, 255u, 128u}},
	    {255u, 128u, {255u, 255u, 255u, 255u}},
	    {1u, 0u, {255u, 255u, 255u, 1u}},
	};
	for (u32 i = 0u; i < (u32)(sizeof(composite_cases) / sizeof(composite_cases[0])); i++)
	{
		u8 pixel[4] = {RG_TEXT_BAKE_SHADOW_R, RG_TEXT_BAKE_SHADOW_G,
		               RG_TEXT_BAKE_SHADOW_B, composite_cases[i].shadow_alpha};
		blit_foreground_layer(&composite_cases[i].foreground_alpha, 1, 1, 0, 0,
		                      pixel, 1u);
		if (memcmp(pixel, composite_cases[i].expected, sizeof(pixel)) != 0)
		{
			fprintf(stderr, "straight-alpha compositing self-test failed for case %u\n", i);
			return 0;
		}
	}

	u8 positive_rows[9] = {1u, 2u, 0u, 3u, 4u, 0u, 5u, 6u, 0u};
	u8 negative_rows[9] = {5u, 6u, 0u, 3u, 4u, 0u, 1u, 2u, 0u};
	u8 expected_alpha[6] = {1u, 2u, 3u, 4u, 5u, 6u};
	u8 copied_alpha[6] = {0};
	FT_Bitmap test_bitmap = {0};
	test_bitmap.width = 2u;
	test_bitmap.rows = 3u;
	test_bitmap.pitch = 3;
	test_bitmap.buffer = positive_rows;
	test_bitmap.num_grays = 256u;
	test_bitmap.pixel_mode = FT_PIXEL_MODE_GRAY;
	if (!copy_bitmap_alpha(&test_bitmap, 2, 3, copied_alpha) ||
	    memcmp(copied_alpha, expected_alpha, sizeof(expected_alpha)) != 0)
	{
		fprintf(stderr, "positive-pitch bitmap self-test failed\n");
		return 0;
	}
	memset(copied_alpha, 0, sizeof(copied_alpha));
	test_bitmap.pitch = -3;
	test_bitmap.buffer = negative_rows;
	if (!copy_bitmap_alpha(&test_bitmap, 2, 3, copied_alpha) ||
	    memcmp(copied_alpha, expected_alpha, sizeof(expected_alpha)) != 0)
	{
		fprintf(stderr, "negative-pitch bitmap self-test failed\n");
		return 0;
	}

	u32 parsed = 0u;
	if (!parse_u32_arg("4294967295", &parsed) || parsed != 0xFFFFFFFFu ||
	    parse_u32_arg("4294967296", &parsed) ||
	    parse_u32_arg("-1", &parsed) ||
	    parse_u32_arg(NULL, &parsed))
	{
		fprintf(stderr, "argument parsing self-test failed\n");
		return 0;
	}

	BakeRange selection_ranges[] = {
	    {0xD7FFu, 0xE000u},
	    {0xD7FFu, 0xD800u},
	    {0xE000u, 0xE001u},
	};
	u8* selection = (u8*)calloc(RG_TEXT_BAKE_SELECTION_BYTES, 1u);
	if (!selection)
	{
		fprintf(stderr, "selection self-test allocation failed\n");
		return 0;
	}
	u32 selected_count = 0u;
	int selection_result = build_selection(selection_ranges,
	                                       (u32)(sizeof(selection_ranges) /
	                                             sizeof(selection_ranges[0])),
	                                       selection,
	                                       &selected_count);
	if (selection_result != 1 || selected_count != 3u ||
	    !selection_contains(selection, 0xD7FFu) ||
	    selection_contains(selection, 0xD800u) ||
	    !selection_contains(selection, 0xE000u) ||
	    !selection_contains(selection, 0xE001u))
	{
		fprintf(stderr, "Unicode scalar selection self-test failed\n");
		free(selection);
		return 0;
	}

	BakeRange oversized_range = {0u, RG_TEXT_BAKE_MAX_CODEPOINTS};
	if (build_selection(&oversized_range, 1u, selection, &selected_count) != -1)
	{
		fprintf(stderr, "selection cap self-test failed\n");
		free(selection);
		return 0;
	}
	free(selection);

	BakedGlyph fallback_glyphs[2] = {0};
	fallback_glyphs[0].codepoint = 0x20u;
	fallback_glyphs[1].codepoint = 0x41u;
	fallback_glyphs[1].bitmap_w = 1;
	fallback_glyphs[1].bitmap_h = 1;
	if (choose_fallback_codepoint(fallback_glyphs, 2u) != 0x41u)
	{
		fprintf(stderr, "fallback selection self-test failed\n");
		return 0;
	}

	char short_path[8];
	if (make_output_path(short_path, sizeof(short_path), "too-long", ".font"))
	{
		fprintf(stderr, "output path truncation self-test failed\n");
		return 0;
	}

	puts("rg_text_bake self-tests passed");
	return 1;
}

static void print_usage(void)
{
	printf("Usage: rg_text_bake.exe [--no-kerning] input.ttf output_base [pixel_size] [first] [last] [padding] [atlas_width]\n");
	printf("       rg_text_bake.exe [--no-kerning] input.ttf output_base [pixel_size] [range_list] [padding] [atlas_width]\n");
}

int main(int argc, char** argv)
{
	if (argc == 2 && strcmp(argv[1], "--self-test") == 0)
	{
		return run_self_tests() ? 0 : 1;
	}

	int include_kerning = 1;
	int first_arg = 1;
	if (argc > 1 && strcmp(argv[1], "--no-kerning") == 0)
	{
		include_kerning = 0;
		first_arg++;
	}
	else if (argc > 1 && argv[1][0] == '-')
	{
		fprintf(stderr, "Unknown option: %s\n", argv[1]);
		print_usage();
		return 1;
	}

	if (argc < first_arg + 2)
	{
		print_usage();
		return 1;
	}

	const char* input_path = argv[first_arg];
	const char* output_base = argv[first_arg + 1];
	int pixel_arg = first_arg + 2;
	u32 pixel_size = 16u;
	u32 padding = 1u;
	u32 atlas_width = 512u;
	BakeRange ranges[16];
	u32 range_count = 1u;
	ranges[0].first = 32u;
	ranges[0].last = 126u;

	if (argc > pixel_arg && !parse_u32_arg(argv[pixel_arg], &pixel_size))
	{
		return 1;
	}
	if (argc > pixel_arg + 1)
	{
		const char* range_arg = argv[pixel_arg + 1];
		if (range_arg_is_list(range_arg))
		{
			if (argc > pixel_arg + 4)
			{
				fprintf(stderr, "Too many bake arguments.\n");
				print_usage();
				return 1;
			}
			if (!parse_range_list(range_arg, ranges,
			                      (u32)RG_ARRAY_COUNT(ranges),
			                      &range_count))
			{
				return 1;
			}
			if (argc > pixel_arg + 2 && !parse_u32_arg(argv[pixel_arg + 2], &padding))
			{
				return 1;
			}
			if (argc > pixel_arg + 3 && !parse_u32_arg(argv[pixel_arg + 3], &atlas_width))
			{
				return 1;
			}
		}
		else
		{
			if (argc > pixel_arg + 5)
			{
				fprintf(stderr, "Too many bake arguments.\n");
				print_usage();
				return 1;
			}
			if (!parse_u32_arg(range_arg, &ranges[0].first))
			{
				return 1;
			}
			if (argc > pixel_arg + 2 &&
			    !parse_u32_arg(argv[pixel_arg + 2], &ranges[0].last))
			{
				return 1;
			}
			if (argc > pixel_arg + 3 && !parse_u32_arg(argv[pixel_arg + 3], &padding))
			{
				return 1;
			}
			if (argc > pixel_arg + 4 && !parse_u32_arg(argv[pixel_arg + 4], &atlas_width))
			{
				return 1;
			}
		}
	}

	if (pixel_size == 0u || pixel_size > 4096u ||
	    padding > 64u || atlas_width < 16u || atlas_width > RG_TEXT_BAKE_MAX_ATLAS_DIM)
	{
		fprintf(stderr, "Invalid bake arguments.\n");
		return 1;
	}

	char rgba_path[1024];
	char rgfont_path[1024];
	char rgba_work_path[1024];
	char rgfont_work_path[1024];
	char rgba_backup_path[1024];
	if (!make_output_path(rgba_path, sizeof(rgba_path), output_base, ".rgba") ||
	    !make_output_path(rgfont_path, sizeof(rgfont_path), output_base, ".font"))
	{
		fprintf(stderr, "Output base path is too long.\n");
		return 1;
	}

	int exit_code = 1;
	int rgba_work_owned = 0;
	int rgfont_work_owned = 0;
	int rgba_backup_owned = 0;
	int preserve_rgba_backup = 0;
	FILE* rgba_work_file = NULL;
	FILE* rgfont_work_file = NULL;
	FILE* rgba_backup_file = NULL;
	BakeLock output_lock;
	memset(&output_lock, 0, sizeof(output_lock));
	u8* selection = NULL;
	FT_Library library = NULL;
	FT_Face face = NULL;
	hb_font_t* hb_font = NULL;
	BakedGlyph* glyphs = NULL;
	u8* pixels = NULL;

	if (!acquire_bake_lock(&output_lock, output_base))
	{
		fprintf(stderr,
		        "Failed to acquire the output lock; another bake may be writing this base.\n");
		goto cleanup;
	}
	if (!reserve_work_file(rgba_work_path, sizeof(rgba_work_path), output_base,
	                       "rgba", "tmp", &rgba_work_file))
	{
		fprintf(stderr, "Failed to reserve a temporary atlas path.\n");
		goto cleanup;
	}
	rgba_work_owned = 1;
	if (!reserve_work_file(rgfont_work_path, sizeof(rgfont_work_path), output_base,
	                       "font", "tmp", &rgfont_work_file))
	{
		fprintf(stderr, "Failed to reserve a temporary metrics path.\n");
		goto cleanup;
	}
	rgfont_work_owned = 1;

	selection = (u8*)calloc(RG_TEXT_BAKE_SELECTION_BYTES, 1u);
	if (!selection)
	{
		fprintf(stderr, "Failed to allocate codepoint selection.\n");
		goto cleanup;
	}

	u32 selected_count = 0u;
	int selection_result = build_selection(ranges, range_count, selection, &selected_count);
	if (selection_result != 1)
	{
		if (selection_result < 0)
		{
			fprintf(stderr,
			        "Bake selects more than %u unique Unicode scalar values; narrow the range.\n",
			        RG_TEXT_BAKE_MAX_CODEPOINTS);
		}
		else
		{
			fprintf(stderr, "Bake range is invalid or contains no Unicode scalar values.\n");
		}
		goto cleanup;
	}

	FT_Error ft_error = FT_Init_FreeType(&library);
	if (ft_error != 0)
	{
		fprintf(stderr, "Failed to initialize FreeType (error %d).\n", (int)ft_error);
		goto cleanup;
	}

	ft_error = FT_New_Face(library, input_path, 0, &face);
	if (ft_error != 0)
	{
		fprintf(stderr, "Failed to open TrueType/OpenType font %s (error %d).\n",
		        input_path, (int)ft_error);
		goto cleanup;
	}
	if (!FT_IS_SCALABLE(face))
	{
		fprintf(stderr, "Font has no scalable outlines: %s\n", input_path);
		goto cleanup;
	}

	ft_error = FT_Select_Charmap(face, FT_ENCODING_UNICODE);
	if (ft_error != 0)
	{
		fprintf(stderr, "Font has no Unicode character map: %s\n", input_path);
		goto cleanup;
	}
	ft_error = FT_Set_Pixel_Sizes(face, 0u, (FT_UInt)pixel_size);
	if (ft_error != 0)
	{
		fprintf(stderr, "FreeType rejected pixel size %u (error %d).\n",
		        pixel_size, (int)ft_error);
		goto cleanup;
	}

	i64 font_span = (i64)face->ascender - (i64)face->descender;
	if (font_span <= 0)
	{
		fprintf(stderr, "Font has invalid vertical metrics: %s\n", input_path);
		goto cleanup;
	}
	int line_height = (int)pixel_size;
	int ascent = 0;
	if (!round_symmetric((double)pixel_size *
	                         (double)face->ascender / (double)font_span,
	                     &ascent))
	{
		fprintf(stderr, "Font has out-of-range vertical metrics: %s\n", input_path);
		goto cleanup;
	}
	if (ascent < 0)
	{
		ascent = 0;
	}
	else if (ascent > line_height)
	{
		ascent = line_height;
	}
	int descent = line_height - ascent;

	glyphs = (BakedGlyph*)calloc(selected_count, sizeof(BakedGlyph));
	if (!glyphs)
	{
		fprintf(stderr, "Failed to allocate glyph table.\n");
		goto cleanup;
	}

	u32 glyph_count = 0u;
	u32 atlas_height = 0u;
	if (!scan_glyphs(face, selection, (int)padding, atlas_width,
	                 glyphs, &glyph_count, &atlas_height, ascent))
	{
		fprintf(stderr,
		        "No supported glyphs fit in the requested atlas; widen it or narrow the range.\n");
		goto cleanup;
	}
	free(selection);
	selection = NULL;

	if (!kerning_work_is_bounded(glyph_count, include_kerning))
	{
		fprintf(stderr,
		        "Kerning extraction is limited to %u supported glyphs (%u found); narrow the range or pass --no-kerning.\n",
		        RG_TEXT_BAKE_MAX_KERNING_GLYPHS, glyph_count);
		goto cleanup;
	}

	if (include_kerning)
	{
		hb_font = hb_ft_font_create_referenced(face);
		if (!hb_font || hb_font == hb_font_get_empty())
		{
			fprintf(stderr, "Failed to create the HarfBuzz font.\n");
			goto cleanup;
		}
		hb_ft_font_set_load_flags(hb_font, RG_TEXT_BAKE_FT_SHAPE_FLAGS);
	}

	u64 atlas_bytes64 = (u64)atlas_width * (u64)atlas_height * 4ull;
	if (atlas_bytes64 > (u64)SIZE_MAX)
	{
		fprintf(stderr, "Atlas too large.\n");
		goto cleanup;
	}

	pixels = (u8*)calloc((size_t)atlas_bytes64, 1u);
	if (!pixels)
	{
		fprintf(stderr, "Failed to allocate atlas.\n");
		goto cleanup;
	}

	if (!blit_glyphs(face, glyphs, glyph_count, pixels, atlas_width))
	{
		fprintf(stderr, "Failed to rasterize glyph atlas.\n");
		goto cleanup;
	}

	u32 kerning_count = 0u;
	if (!write_rgfont(rgfont_work_file, hb_font, glyphs, glyph_count,
	                  atlas_width, atlas_height, line_height, ascent, descent,
	                  include_kerning, &kerning_count))
	{
		fprintf(stderr, "Failed to stage metrics: %s\n", rgfont_work_path);
		goto cleanup;
	}
	if (!write_file(rgba_work_file, pixels, (size_t)atlas_bytes64))
	{
		fprintf(stderr, "Failed to stage atlas: %s\n", rgba_work_path);
		goto cleanup;
	}
	if (!close_work_file(&rgfont_work_file))
	{
		fprintf(stderr, "Failed to close staged metrics: %s\n", rgfont_work_path);
		goto cleanup;
	}
	if (!close_work_file(&rgba_work_file))
	{
		fprintf(stderr, "Failed to close staged atlas: %s\n", rgba_work_path);
		goto cleanup;
	}

	if (!reserve_work_file(rgba_backup_path, sizeof(rgba_backup_path), output_base,
	                       "rgba", "rollback", &rgba_backup_file))
	{
		fprintf(stderr, "Failed to reserve an atlas rollback path.\n");
		goto cleanup;
	}
	rgba_backup_owned = 1;
	int had_previous_rgba = 0;
	if (!backup_file_if_present(rgba_path, rgba_backup_file,
	                            &had_previous_rgba))
	{
		fprintf(stderr, "Failed to back up the existing atlas before commit: %s\n",
		        rgba_path);
		goto cleanup;
	}
	if (!close_work_file(&rgba_backup_file))
	{
		fprintf(stderr, "Failed to close the atlas rollback file: %s\n",
		        rgba_backup_path);
		goto cleanup;
	}

	if (!replace_file(rgba_work_path, rgba_path))
	{
		fprintf(stderr, "Failed to replace atlas: %s\n", rgba_path);
		goto cleanup;
	}
	rgba_work_owned = 0;

	/* Metrics are the final commit marker for the staged output pair. */
	if (!replace_file(rgfont_work_path, rgfont_path))
	{
		fprintf(stderr, "Failed to replace metrics: %s\n", rgfont_path);
		if (had_previous_rgba)
		{
			if (replace_file(rgba_backup_path, rgba_path))
			{
				rgba_backup_owned = 0;
			}
			else
			{
				preserve_rgba_backup = 1;
				fprintf(stderr,
				        "Failed to roll back the atlas; recovery copy preserved at %s\n",
				        rgba_backup_path);
			}
		}
		else
		{
			errno = 0;
			if (remove(rgba_path) != 0 && errno != ENOENT)
			{
				fprintf(stderr, "Failed to remove the newly committed atlas during rollback: %s\n",
				        rgba_path);
			}
		}
		goto cleanup;
	}
	rgfont_work_owned = 0;

	printf("Wrote %s (%ux%u RGBA8, %zu bytes)\n", rgba_path, atlas_width, atlas_height,
	       (size_t)atlas_bytes64);
	printf("Wrote %s (%u glyphs, %u kerning pairs, %d line height)\n",
	       rgfont_path, glyph_count, kerning_count, line_height);
	exit_code = 0;

cleanup:
	if (!close_work_file(&rgba_work_file))
	{
		fprintf(stderr, "Failed to close temporary atlas: %s\n", rgba_work_path);
	}
	if (!close_work_file(&rgfont_work_file))
	{
		fprintf(stderr, "Failed to close temporary metrics: %s\n", rgfont_work_path);
	}
	if (!close_work_file(&rgba_backup_file))
	{
		fprintf(stderr, "Failed to close atlas rollback file: %s\n", rgba_backup_path);
	}
	free(pixels);
	free(glyphs);
	free(selection);
	if (hb_font)
	{
		hb_font_destroy(hb_font);
	}
	if (face)
	{
		FT_Done_Face(face);
	}
	if (library)
	{
		FT_Done_FreeType(library);
	}
	if (rgba_work_owned)
	{
		errno = 0;
		if (remove(rgba_work_path) != 0 && errno != ENOENT)
		{
			fprintf(stderr, "Failed to remove temporary atlas: %s\n", rgba_work_path);
		}
	}
	if (rgfont_work_owned)
	{
		errno = 0;
		if (remove(rgfont_work_path) != 0 && errno != ENOENT)
		{
			fprintf(stderr, "Failed to remove temporary metrics: %s\n", rgfont_work_path);
		}
	}
	if (rgba_backup_owned && !preserve_rgba_backup)
	{
		errno = 0;
		if (remove(rgba_backup_path) != 0 && errno != ENOENT)
		{
			fprintf(stderr, "Failed to remove atlas rollback file: %s\n",
			        rgba_backup_path);
		}
	}
	release_bake_lock(&output_lock);
	return exit_code;
}
