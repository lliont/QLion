/** @file src/video/video_ql.c
 * Sinclair QL + QLion Gold card video driver for OpenDUNE.
 *
 * Display : QLion mode 33, 512x256 chunky 8bpp, pixel = RRRGGGBB,
 *           VRAM at $4C0000, 512 bytes per line, no palette registers.
 * The game's 320x200 8bpp buffer is centred and converted through a
 * 256-entry VGA-palette -> RRRGGGBB lookup table on every update.
 *
 * Input  : mouse via QL_Pointer_Read() (Pointer Environment, see below),
 *          keyboard via a QDOS con_ channel, translated to PC set-1
 *          scancodes for Input_EventHandler().
 */

#include <string.h>
#include <stdlib.h>
#include <qdos.h>

#include "types.h"
#include "video.h"
#include "../gfx.h"
#include "../input/input.h"
#include "../input/mouse.h"
#include "../os/error.h"
#include "../inifile.h"
#include "../timer.h"
#include "../os/endian.h"
#include "video_ql.h"

/* ------------------------------------------------------------------ */
/* QLion hardware                                                       */
/* ------------------------------------------------------------------ */
#define QL_VMODE_REG        ((volatile uint8 *)0x80000EUL)	/* 8388622 */
#define QL_VMODE_512x256x8  33
#define QL_VRAM             ((uint8 *)0x4C0000UL)
#define QL_VRAM_PITCH       512
#define QL_VRAM_LINES       256
#define QL_X_OFFSET         ((QL_VRAM_PITCH - SCREEN_WIDTH) / 2)	/* 96 */
#define QL_Y_OFFSET         ((QL_VRAM_LINES - SCREEN_HEIGHT) / 2)	/* 28 */

/* Supplied elsewhere (ql_pointer.c / .s) : read the Pointer Environment
 * pointer. Returns true on success; x/y are absolute PE coordinates,
 * buttons bit 0 = left, bit 1 = right. Only the deltas are used here,
 * so the PE coordinate scale does not matter. */
extern bool QL_Pointer_Read(int16 *x, int16 *y, uint8 *buttons);
extern void QL_Pointer_Uninit(void);

/* ------------------------------------------------------------------ */
/* State                                                                */
/* ------------------------------------------------------------------ */
static uint8 *s_framebuffer = NULL;
static uint8  s_savedMode = 0;
static uint8  s_lut[256];		/* palette index -> RRRGGGBB */
static uint16 s_lut_hi[256];		/* same, shifted into the high byte (ql_blit.s) */
static uint16 s_screenOffset = 0;
static bool   s_screen_needrepaint = true;

static chanid_t s_con = -1;		/* keyboard input channel */

static int  s_mouse_x = SCREEN_WIDTH / 2;
static int  s_mouse_y = SCREEN_HEIGHT / 2;
static int  s_mouse_x_min = 0, s_mouse_x_max = SCREEN_WIDTH - 1;
static int  s_mouse_y_min = 0, s_mouse_y_max = SCREEN_HEIGHT - 1;
static int16 s_pe_last_x = 0, s_pe_last_y = 0;
static bool s_pe_valid = false;
static uint8 s_mouse_buttons = 0;
static int  s_mouse_speed = 130;	/* percent, opendune.ini : mousespeed=130 */
static int  s_frac_x = 0, s_frac_y = 0;	/* leftover fraction, in 1/100 pixel */

/* Hardware-sprite cursor (opendune.ini : hwcursor=1) */
#define SPR_PARAM(n)   ((volatile uint8 *)(0xC00000UL + (n) * 4))
#define SPR_DATA(n)    ((volatile uint8 *)(0xC00000UL + 128 + (n) * 128))
#define SPR_PAL(n)     ((volatile uint8 *)(12648448UL + (n) * 16))
#define CUR_MAX        32			/* game pixels, up to 2 sprites tall */
static bool  s_hwCursor = false;
static uint8 s_curCols[15];		/* game palette index of each sprite colour */
static int   s_curNumCols = 0;
static int   s_curHeight = 0;		/* game pixels */
static int   s_curFormat = 2;		/* opendune.ini hwcursorfmt : bit 0 = swap nibbles,
					 * bit 1 = reverse the 8 bytes of each row */

/* FPS counter (opendune.ini : showfps=1), drawn in the top border */
static bool   s_showFPS = false;
static uint16 s_frames = 0;
static uint32 s_fpsTime = 0;

/* ------------------------------------------------------------------ */
/* Palette                                                              */
/* ------------------------------------------------------------------ */

/* VGA 6-bit component (0..63) -> rounded 3-bit / 2-bit value */
static uint8 QL_PalToRGB332(uint8 r, uint8 g, uint8 b)
{
	uint8 r3 = (uint8)(((r & 63) * 7 + 31) / 63);
	uint8 g3 = (uint8)(((g & 63) * 7 + 31) / 63);
	uint8 b2 = (uint8)(((b & 63) * 3 + 31) / 63);
	return (uint8)((r3 << 5) | (g3 << 2) | b2);
}

void Video_SetPalette(void *palette, int from, int length)
{
	uint8 *p = palette;
	int i;

	for (i = from; i < from + length && i < 256; i++, p += 3) {
		uint8 c = QL_PalToRGB332(p[0], p[1], p[2]);
		if (s_lut[i] != c) {
			s_lut[i] = c;
			s_lut_hi[i] = (uint16)(c << 8);
			/* no hardware palette : everything already on screen
			 * must be converted again with the new table */
			s_screen_needrepaint = true;
		}
	}

	if (s_hwCursor) {	/* keep the cursor colours in step with fades */
		for (i = 0; i < s_curNumCols; i++) {
			SPR_PAL(0)[i] = s_lut[s_curCols[i]];
			SPR_PAL(1)[i] = s_lut[s_curCols[i]];
		}
	}
}

/* ------------------------------------------------------------------ */
/* Blit                                                                 */
/* ------------------------------------------------------------------ */

/* Portable replacements for __builtin_ctz/__builtin_clz, which older
 * gcc versions (e.g. 2.95-based qdos-gcc) do not have. b is never 0. */
static int QL_FirstBit(uint32 b)
{
	int n = 0;
	while ((b & 1) == 0) { b >>= 1; n++; }
	return n;
}

static int QL_LastBit(uint32 b)
{
	int n = 31;
	while ((b & 0x80000000UL) == 0) { b <<= 1; n--; }
	return n;
}

/* Convert a rectangle. width must be even, src and dst word aligned.
 * Word writes halve the number of bus cycles on the 16-bit bus.
 * Good candidate for a hand-written 68000 routine later. */
extern void QL_BlitRectAsm(const uint8 *src, uint8 *dst, int width, int height,
                           const uint8 *lut, const uint16 *lut_hi);

static void QL_BlitRect(const uint8 *src, uint8 *dst, int width, int height)
{
	const uint8 *lut = s_lut;

	if (width <= 0 || height <= 0) return;

	/* fast path : ql_blit.s handles widths that are a multiple of 16,
	 * which is every case OpenDUNE produces (16-pixel dirty blocks) */
	if ((width & 15) == 0) {
		QL_BlitRectAsm(src, dst, width, height, s_lut, s_lut_hi);
		return;
	}

	while (height-- > 0) {
		const uint8 *s = src;
		uint16 *d = (uint16 *)dst;
		int n = width >> 1;

		while (n-- > 0) {
			*d++ = (uint16)((lut[s[0]] << 8) | lut[s[1]]);
			s += 2;
		}
		src += SCREEN_WIDTH;
		dst += QL_VRAM_PITCH;
	}
}

/* ------------------------------------------------------------------ */
/* Keyboard : QDOS keycode -> PC set-1 scancode                         */
/* ------------------------------------------------------------------ */
/* Use RIGHT shift : input.c only checks RSHIFT (Dune II code 0x39)
 * when building the shift flag, LSHIFT (0x2A) would give lower case. */
#define SC_SHIFT 0x36

static uint8 QL_KeyToScancode(uint8 c, bool *shift)
{
	static const char row1[] = "qwertyuiop";	/* 0x10.. */
	static const char row2[] = "asdfghjkl";		/* 0x1E.. */
	static const char row3[] = "zxcvbnm";		/* 0x2C.. */
	const char *p;

	*shift = false;
	if (c >= 'A' && c <= 'Z') { *shift = true; c = (uint8)(c - 'A' + 'a'); }

	if ((p = strchr(row1, c)) != NULL && c != 0) return (uint8)(0x10 + (p - row1));
	if ((p = strchr(row2, c)) != NULL && c != 0) return (uint8)(0x1E + (p - row2));
	if ((p = strchr(row3, c)) != NULL && c != 0) return (uint8)(0x2C + (p - row3));
	if (c >= '1' && c <= '9') return (uint8)(0x02 + (c - '1'));

	switch (c) {
		case '0':  return 0x0B;
		case '-':  return 0x0C;
		case '=':  return 0x0D;
		case '[':  return 0x1A;
		case ']':  return 0x1B;
		case ';':  return 0x27;
		case '\'': return 0x28;
		case '`':  return 0x29;
		case '\\': return 0x2B;
		case ',':  return 0x33;
		case '.':  return 0x34;
		case '/':  return 0x35;
		case ' ':  return 0x39;
		case 10:   return 0x1C;	/* ENTER */
		case 27:   return 0x01;	/* ESC */
		case 9:    return 0x0F;	/* TAB */
		case 0xC2: return 0x0E;	/* CTRL+LEFT = delete left -> BACKSPACE */
		case 0xC0: return 0x4B;	/* LEFT */
		case 0xC8: return 0x4D;	/* RIGHT */
		case 0xD0: return 0x48;	/* UP */
		case 0xD8: return 0x50;	/* DOWN */
		case 0xE8: return 0x3B;	/* F1 */
		case 0xEC: return 0x3C;	/* F2 */
		case 0xF0: return 0x3D;	/* F3 */
		case 0xF4: return 0x3E;	/* F4 */
		case 0xF8: return 0x3F;	/* F5 */
		default:   return 0;
	}
}

static void QL_PollKeyboard(void)
{
	char c;
	int guard = 8;	/* max keys per tick */

	if (s_con < 0) return;

	while (guard-- > 0 && io_fbyte(s_con, 0, &c) == 0) {
		bool shift;
		uint8 sc = QL_KeyToScancode((uint8)c, &shift);
		if (sc == 0) continue;
		if (shift) Input_EventHandler(SC_SHIFT);
		Input_EventHandler(sc);			/* press   */
		Input_EventHandler((uint8)(sc | 0x80));	/* release */
		if (shift) Input_EventHandler(SC_SHIFT | 0x80);
	}
}

/* ------------------------------------------------------------------ */
/* Mouse                                                                */
/* ------------------------------------------------------------------ */
static void QL_ClampMouse(void)
{
	if (s_mouse_x < s_mouse_x_min) s_mouse_x = s_mouse_x_min;
	else if (s_mouse_x > s_mouse_x_max) s_mouse_x = s_mouse_x_max;
	if (s_mouse_y < s_mouse_y_min) s_mouse_y = s_mouse_y_min;
	else if (s_mouse_y > s_mouse_y_max) s_mouse_y = s_mouse_y_max;
}

static void QL_PollMouse(void)
{
	int16 x, y;
	uint8 buttons;
	bool changed = false;

	if (!QL_Pointer_Read(&x, &y, &buttons)) return;

	if (!s_pe_valid) {
		s_pe_valid = true;
	} else if (x != s_pe_last_x || y != s_pe_last_y) {
		int mx, my;
		/* scale by s_mouse_speed %, keeping the remainder for next time
		 * so slow movements are not lost or rounded into jitter */
		s_frac_x += (int16)(x - s_pe_last_x) * s_mouse_speed;
		s_frac_y += (int16)(y - s_pe_last_y) * s_mouse_speed;
		mx = s_frac_x / 100;
		my = s_frac_y / 100;
		s_frac_x -= mx * 100;
		s_frac_y -= my * 100;
		if (mx != 0 || my != 0) {
			s_mouse_x += mx;
			s_mouse_y += my;
			QL_ClampMouse();
			changed = true;
		}
	}
	s_pe_last_x = x;
	s_pe_last_y = y;

	if (buttons != s_mouse_buttons) {
		s_mouse_buttons = buttons;
		changed = true;
	}

	if (changed) {
		Mouse_EventHandler(s_mouse_x, s_mouse_y,
		                   (s_mouse_buttons & 1) != 0, (s_mouse_buttons & 2) != 0);
	}
}

void Video_Mouse_SetPosition(uint16 x, uint16 y)
{
	s_mouse_x = x;
	s_mouse_y = y;
	QL_ClampMouse();
}

void Video_Mouse_SetRegion(uint16 minX, uint16 maxX, uint16 minY, uint16 maxY)
{
	s_mouse_x_min = minX;
	s_mouse_x_max = maxX;
	s_mouse_y_min = minY;
	s_mouse_y_max = maxY;
	QL_ClampMouse();
}

/* ------------------------------------------------------------------ */
/* Hardware-sprite mouse cursor                                         */
/* ------------------------------------------------------------------ */
/* QLion sprites : 16x16, 15 colours + $F transparent, 4 bits per pixel,
 * low nibble = left pixel. The sprite plane is 256 wide over the whole
 * 512-pixel display, so one sprite pixel = 2 screen (= game) pixels.
 * Sprite 0 holds the top 16 rows of the cursor, sprite 1 the next 16. */

bool QL_HWCursor_Enabled(void)
{
	return s_hwCursor;
}

static int QL_CursorColour(uint8 gameIndex)
{
	int i, best = 0, bestDist = 1000;
	uint8 c = s_lut[gameIndex];

	for (i = 0; i < s_curNumCols; i++) {
		if (s_curCols[i] == gameIndex) return i;
	}
	if (s_curNumCols < 15) {
		s_curCols[s_curNumCols] = gameIndex;
		return s_curNumCols++;
	}
	/* more than 15 colours : use the closest one already in the sprite */
	for (i = 0; i < 15; i++) {
		uint8 o = s_lut[s_curCols[i]];
		int dr = ((c >> 5) & 7) - ((o >> 5) & 7);
		int dg = ((c >> 2) & 7) - ((o >> 2) & 7);
		int db = (c & 3) - (o & 3);
		int d = dr * dr + dg * dg + db * db * 2;
		if (d < bestDist) { bestDist = d; best = i; }
	}
	return best;
}

void QL_HWCursor_SetImage(const uint8 *sprite)
{
	static int16 img[CUR_MAX][CUR_MAX];	/* game palette index, -1 = transparent */
	const uint8 *data, *pal = NULL;
	uint16 flags, width, height;
	int x, y, n, i;

	if (!s_hwCursor || sprite == NULL) return;

	flags  = READ_LE_UINT16(sprite);
	height = sprite[2];
	width  = READ_LE_UINT16(sprite + 3);
	data   = sprite + 10;
	if ((flags & 0x1) != 0) { pal = data; data += 16; }

	/* decode the RLE rows (0, count = transparent run) */
	for (y = 0; y < CUR_MAX; y++) for (x = 0; x < CUR_MAX; x++) img[y][x] = -1;
	for (y = 0; y < height; y++) {
		x = 0;
		while (x < width) {
			uint8 v = *data++;
			if (v == 0) {
				x += *data++;
			} else {
				if (x < CUR_MAX && y < CUR_MAX) img[y][x] = pal != NULL ? pal[v & 15] : v;
				x++;
			}
		}
	}
	s_curHeight = (height > CUR_MAX) ? CUR_MAX : height;

	/* build the two sprite patterns : each sprite pixel takes the first
	 * opaque pixel of the game pixel pair it covers */
	s_curNumCols = 0;
	for (n = 0; n < 2; n++) {
		volatile uint8 *d = SPR_DATA(n);
		for (y = 0; y < 16; y++) {
			int gy = n * 16 + y;
			uint8 row[8];
			for (i = 0; i < 8; i++) {
				uint8 nib[2];
				int k;
				for (k = 0; k < 2; k++) {
					int sx = i * 2 + k;		/* sprite pixel 0..15 */
					int16 p = -1;
					if (gy < CUR_MAX) {
						p = img[gy][sx * 2];
						if (p < 0) p = img[gy][sx * 2 + 1];
					}
					nib[k] = (uint8)((p < 0) ? 0xF : QL_CursorColour((uint8)p));
				}
				if (s_curFormat & 1) row[i] = (uint8)((nib[0] << 4) | nib[1]);	/* high nibble = left */
				else                 row[i] = (uint8)((nib[1] << 4) | nib[0]);	/* low nibble = left */
			}
			for (i = 0; i < 8; i++) {
				*d++ = (s_curFormat & 2) ? row[7 - i] : row[i];
			}
		}
	}
	for (i = 0; i < s_curNumCols; i++) {
		SPR_PAL(0)[i] = s_lut[s_curCols[i]];
		SPR_PAL(1)[i] = s_lut[s_curCols[i]];
	}
}

void QL_HWCursor_Show(int left, int top)
{
	int sx = (QL_X_OFFSET + left) >> 1;
	int sy = QL_Y_OFFSET + top;

	if (!s_hwCursor) return;
	if (sx < 0) sx = 0;
	if (sx > 255) sx = 255;
	if (sy < 0) sy = 0;

	SPR_PARAM(0)[1] = (uint8)sx;
	SPR_PARAM(0)[2] = (uint8)(sy > 255 ? 255 : sy);
	SPR_PARAM(0)[0] = 1;

	if (s_curHeight > 16 && sy + 16 <= 255) {
		SPR_PARAM(1)[1] = (uint8)sx;
		SPR_PARAM(1)[2] = (uint8)(sy + 16);
		SPR_PARAM(1)[0] = 1;
	} else {
		SPR_PARAM(1)[0] = 0;
	}
}

void QL_HWCursor_Hide(void)
{
	if (!s_hwCursor) return;
	SPR_PARAM(0)[0] = 0;
	SPR_PARAM(1)[0] = 0;
}

/* ------------------------------------------------------------------ */
/* FPS counter                                                          */
/* ------------------------------------------------------------------ */
/* 3x5 digits, one row per byte, bit 2 = left pixel */
static const uint8 s_digit[10][5] = {
	{7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7}, {5,5,7,1,1},
	{7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1}, {7,5,7,5,7}, {7,5,7,1,7}
};

/* Draw a number at the top-left of the picture area, in the border above
 * it (lines 8..12), white on black, pixels doubled for readability. */
static void QL_DrawFPS(uint16 value)
{
	uint8 *base = QL_VRAM + 8 * QL_VRAM_PITCH + QL_X_OFFSET;
	char txt[4];
	int i, row, col, n = 0;

	if (value > 999) value = 999;
	if (value >= 100) txt[n++] = (char)(value / 100);
	if (value >= 10)  txt[n++] = (char)((value / 10) % 10);
	txt[n++] = (char)(value % 10);

	for (row = 0; row < 5; row++) {
		uint8 *p = base + row * QL_VRAM_PITCH;
		memset(p, 0, 3 * 8);			/* clear 3 digits */
		for (i = 0; i < n; i++) {
			uint8 bits = s_digit[(int)txt[i]][row];
			for (col = 0; col < 3; col++) {
				uint8 c = (bits & (4 >> col)) ? 0xFF : 0x00;
				p[i * 8 + col * 2]     = c;
				p[i * 8 + col * 2 + 1] = c;
			}
		}
	}
}

static void QL_CountFrame(void)
{
	uint32 now;
	s_frames++;
	now = Timer_GetTime();
	if (now - s_fpsTime >= 1000) {
		QL_DrawFPS((uint16)((uint32)s_frames * 1000 / (now - s_fpsTime)));
		s_frames = 0;
		s_fpsTime = now;
	}
}

/* ------------------------------------------------------------------ */
/* Driver entry points                                                  */
/* ------------------------------------------------------------------ */
bool Video_Init(int screen_magnification, VideoScaleFilter filter)
{
	int i;

	VARIABLE_NOT_USED(screen_magnification);
	VARIABLE_NOT_USED(filter);

	/* + 4 lines, as on the Atari : explosions draw slightly past the end */
	s_framebuffer = calloc(1, SCREEN_WIDTH * (SCREEN_HEIGHT + 4));
	if (s_framebuffer == NULL) {
		Error("Failed to allocate %d bytes.\n", SCREEN_WIDTH * (SCREEN_HEIGHT + 4));
		return false;
	}

	s_showFPS = (IniFile_GetInteger("showfps", 0) != 0);
	s_hwCursor = (IniFile_GetInteger("hwcursor", 0) != 0);
	s_curFormat = IniFile_GetInteger("hwcursorfmt", 2) & 3;	/* 2 = QLion layout */
	if (s_hwCursor) {
		SPR_PARAM(0)[0] = 0;
		SPR_PARAM(1)[0] = 0;
	}
	s_fpsTime = Timer_GetTime();

	s_mouse_speed = IniFile_GetInteger("mousespeed", 130);
	if (s_mouse_speed < 10) s_mouse_speed = 10;
	if (s_mouse_speed > 1000) s_mouse_speed = 1000;

	/* grey ramp until the game sets its own palette */
	for (i = 0; i < 256; i++) {
		s_lut[i] = QL_PalToRGB332(i >> 2, i >> 2, i >> 2);
		s_lut_hi[i] = (uint16)(s_lut[i] << 8);
	}

	/* keyboard : a tiny console window; the cursor makes it the
	 * channel that receives keystrokes */
	s_con = io_open("con_4x2a0x0", 0);
	if (s_con >= 0) sd_cure(s_con, -1);

	s_savedMode = *QL_VMODE_REG;
	*QL_VMODE_REG = QL_VMODE_512x256x8;
	memset(QL_VRAM, 0, QL_VRAM_PITCH * QL_VRAM_LINES);

	s_screen_needrepaint = true;
	return true;
}

void Video_Uninit(void)
{
	QL_HWCursor_Hide();
	QL_Pointer_Uninit();
	*QL_VMODE_REG = s_savedMode;
	if (s_con >= 0) { io_close(s_con); s_con = -1; }
	free(s_framebuffer);
	s_framebuffer = NULL;
}

void Video_SwitchFPSDisplay(uint8 key)
{
	if (key & 0x80) {	/* on key release */
		s_showFPS = !s_showFPS;
		if (!s_showFPS) {
			int row;
			for (row = 0; row < 5; row++) {
				memset(QL_VRAM + (8 + row) * QL_VRAM_PITCH + QL_X_OFFSET, 0, 3 * 8);
			}
		}
	}
}

void Video_Tick(void)
{
	uint8 *data = GFX_Screen_Get_ByIndex(SCREEN_0);
	uint8 *screen = QL_VRAM + QL_Y_OFFSET * QL_VRAM_PITCH + QL_X_OFFSET;

	QL_PollMouse();
	QL_PollKeyboard();

	if (!GFX_Screen_IsDirty(SCREEN_0) && !s_screen_needrepaint) return;

	data += (s_screenOffset << 2);

	if (s_screen_needrepaint) {
		QL_BlitRect(data, screen, SCREEN_WIDTH, SCREEN_HEIGHT);
	} else {
		struct dirty_area *area = GFX_Screen_GetDirtyArea(SCREEN_0);

		if (area == NULL) {
			QL_BlitRect(data, screen, SCREEN_WIDTH, SCREEN_HEIGHT);
		} else {
			uint16 bottom = area->bottom;
			if (area->top >= bottom) {
				GFX_Screen_SetClean(SCREEN_0);
				return;
			}
			if (bottom > SCREEN_HEIGHT) bottom = SCREEN_HEIGHT;
			data   += area->top * SCREEN_WIDTH;
			screen += area->top * QL_VRAM_PITCH;
#ifdef GFX_STORE_DIRTY_AREA_BLOCKS
			{
				/* g_dirty_blocks[y] : one bit per 16-pixel block of line y */
				uint16 y;
				for (y = area->top; y < bottom; y++) {
					uint32 b = g_dirty_blocks[y];
					if (b != 0) {
						int left  = QL_FirstBit(b) << 4;
						int width = ((QL_LastBit(b) + 1) << 4) - left;
						QL_BlitRect(data + left, screen + left, width, 1);
					}
					data   += SCREEN_WIDTH;
					screen += QL_VRAM_PITCH;
				}
			}
#else
			{
				int left  = area->left & ~0xf;
				int width = ((area->right + 0xf) & ~0xf) - left;
				if (left + width > SCREEN_WIDTH) width = SCREEN_WIDTH - left;
				QL_BlitRect(data + left, screen + left, width, bottom - area->top);
			}
#endif
		}
	}

	GFX_Screen_SetClean(SCREEN_0);
	s_screen_needrepaint = false;

	if (s_showFPS) QL_CountFrame();
}

/*
 * VGA start-address change (granularity 4 bytes). Done in software like
 * the Atari TT/ST path. Could later use the QLion VRAM base offset
 * register at $800062 instead.
 */
void Video_SetOffset(uint16 offset)
{
	s_screenOffset = offset;
	s_screen_needrepaint = true;
}

void *Video_GetFrameBuffer(uint16 size)
{
	VARIABLE_NOT_USED(size);
	return s_framebuffer;
}
