/* Copyright (C): Baptiste Fouques 2016 */

/* This file is part of Shadow Library. */

/* Shadow Library is free software: you can redistribute it and/or modify */
/* it under the terms of the GNU Lesser General Public License as published by */
/* the Free Software Foundation, either version 3 of the License, or */
/* (at your option) any later version. */

/* Foobar is distributed in the hope that it will be useful, */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the */
/* GNU LesserGeneral Public License for more details. */

/* You should have received a copy of the GNU Lesser General Public License */
/* along with Shadow Library.  If not, see <http://www.gnu.org/licenses/>.  */

#include <pebble.h>

#include "libshadow.h"

#define gpoint_invert(o) ((GPoint){.x=-((o).x), .y=-((o).y)})
#define gpoint_add(a, b) ((GPoint){.x=((a).x) + ((b).x), .y=((a).y) + ((b).y)})
#define gpoint_sub(a, b) ((GPoint){.x=((a).x) - ((b).x), .y=((a).y) - ((b).y)})
#define gpoint_in_rect(o, r) (((r).origin.x) <= ((o).x) && ((o).x) < ((r).origin.x) + ((r).size.w) && ((r).origin.y) <= ((o).y) && ((o).y) < ((r).origin.y) + ((r).size.h))
const GPoint gpoint_null = (GPoint) {.x = 0, .y = 0};

typedef unsigned int uint_t;

#if defined (PBL_RECT)
static uint_t g_min_x, g_max_x;
#else
typedef struct {
  uint_t data_delta;
  uint8_t min_x;
  uint8_t max_x;
}GBitmapDataRowDelta;
static GBitmapDataRowDelta *g_row_info = NULL;
#endif

static inline uint_t row_min_x (const uint_t y);
static inline uint_t row_max_x (const uint_t y);
static inline void set_fb_pixel(uint8_t * const data, const GPoint point, const GPoint translation, const GColor color);
static inline GColor get_fb_pixel(const uint8_t *data, GPoint point, GPoint translation);

GColor get_light_shadow_color (const GColor c);
GColor get_light_bright_color (const GColor c);

////////////////////////////////////////////////////////////////////////////////

typedef struct {
  int base_z, inner_z, outer_z;
} GShadow_Information;
static GShadow_Information shadow_object_list [GShadowMaxRef];
static GShadow shadow_object_current = 0;

static uint8_t *fb_data;

static GBitmap *shadow_bitmap = NULL;
static uint8_t *shadow_bitmap_data;
static size_t shadow_bitmap_size;
static uint16_t shadow_bitmap_bytes_per_row;
static GBitmapFormat shadow_bitmap_format;


inline GColor8 gcolor (const GShadow shadow) {
  return (GColor8) {.argb = (uint8_t) shadow};
}

GShadow new_shadowing_object (const int base_z, const int inner_z, const int outer_z) {
  GShadow r = shadow_object_current;
  shadow_object_list [shadow_object_current] = 
    (GShadow_Information) { 
    .base_z  = base_z,
    .inner_z = inner_z,
    .outer_z = outer_z};
  shadow_object_current = (shadow_object_current + 1) % GShadowMaxRef;

  return GShadowUnclear | r;
}

void reset_shadow () {
  memset (shadow_bitmap_data, GShadowClear, shadow_bitmap_size);
}

void switch_to_shadow_ctx (GContext * const ctx) {
  GBitmap * const fb = graphics_capture_frame_buffer(ctx); {
    if (shadow_bitmap == NULL) {
      const GRect shadow_bitmap_bounds = gbitmap_get_bounds (fb);
      shadow_bitmap_format = gbitmap_get_format (fb);
      shadow_bitmap_bytes_per_row = gbitmap_get_bytes_per_row (fb);
      shadow_bitmap_size = shadow_bitmap_bounds.size.h * shadow_bitmap_bounds.size.w;

      shadow_bitmap_data = malloc (shadow_bitmap_size);
      memset (shadow_bitmap_data, GShadowClear, shadow_bitmap_size);

      fb_data = gbitmap_get_data (fb);

#if defined(PBL_RECT)
      g_min_x = 0;
      g_max_x = shadow_bitmap_bounds.size.w;
#else
      g_row_info = malloc (sizeof (GBitmapDataRowDelta) * shadow_bitmap_bounds.size.h);
      for(uint_t y = 0; y < (uint_t)shadow_bitmap_bounds.size.h; y++) {
        const GBitmapDataRowInfo info = gbitmap_get_data_row_info(fb, y);
        g_row_info [y] = (GBitmapDataRowDelta){.min_x = info.min_x, .max_x = info.max_x, .data_delta = info.data - fb_data};
      }
#endif

      shadow_bitmap = gbitmap_create_with_data (shadow_bitmap_data);
      gbitmap_set_data (shadow_bitmap, shadow_bitmap_data, shadow_bitmap_format, shadow_bitmap_bytes_per_row,true);
      gbitmap_set_bounds (shadow_bitmap, shadow_bitmap_bounds);
    }

    gbitmap_set_data (fb, shadow_bitmap_data, shadow_bitmap_format, shadow_bitmap_bytes_per_row, true);

  } graphics_release_frame_buffer(ctx, fb);
  graphics_context_set_antialiased (ctx, false);
}

void revert_to_fb_ctx (GContext * const ctx) {
  GBitmap *fb = graphics_capture_frame_buffer(ctx); {
    gbitmap_set_data (fb, fb_data, shadow_bitmap_format, shadow_bitmap_bytes_per_row, true);
  } graphics_release_frame_buffer(ctx, fb);
  graphics_context_set_antialiased (ctx, true);
}

void destroy_shadow_ctx () {
#if defined(PBL_ROUND)
  if (g_row_info) {
    free (g_row_info);
    g_row_info = NULL;
  }
#endif
  gbitmap_destroy (shadow_bitmap);
};

void create_shadow (GContext * const ctx, const int32_t angle) {
  // compute x and y offset from angle and height (z)
  const int offset_x = (- cos_lookup (angle) * GShadowMaxValue * 2) / TRIG_MAX_RATIO;
  const int offset_y = (sin_lookup (angle) * GShadowMaxValue * 2) / TRIG_MAX_RATIO;

  GBitmap * const fb = graphics_capture_frame_buffer(ctx);
  {
    // Manipulate the image data...
    const GRect bounds = gbitmap_get_bounds(fb);
    // Iterate over all rows

    for(uint_t y = bounds.origin.y; y < (uint_t)(bounds.origin.y + bounds.size.h); y++) {
      const uint_t max_x = row_max_x (y);
      for(uint_t x = row_min_x (y); x <= max_x; x++) {
        const GPoint origin = (GPoint) {.x = x, .y = y};
        const GShadow id = (GShadow)get_fb_pixel (shadow_bitmap_data, origin, gpoint_null).argb;
        if (id != GShadowClear) {
          const GShadow ref = id & GShadowMaxRef;
          const GShadow base_z  = shadow_object_list [ref].base_z;
          const GShadow inner_z = shadow_object_list [ref].inner_z;
          const GShadow outer_z = shadow_object_list [ref].outer_z;

          if (inner_z) {
            const GPoint translation = (GPoint) {.x = (offset_x * inner_z) / GShadowMaxValue,
                                                 .y = (offset_y * inner_z) / GShadowMaxValue};

            if (gpoint_in_rect (gpoint_add (origin,translation), bounds) &&
                gpoint_in_rect (gpoint_sub (origin,translation), bounds)) {
              const int dec_id_plus  = (GShadow) get_fb_pixel (shadow_bitmap_data, origin, translation).argb;
              const GShadow dec_base_plus  = shadow_object_list [dec_id_plus & GShadowMaxRef].base_z;

              const int dec_id_minus = (GShadow) get_fb_pixel (shadow_bitmap_data, origin, gpoint_invert (translation)).argb;
              const GShadow dec_base_minus  = shadow_object_list [dec_id_minus & GShadowMaxRef].base_z;

              // we are still on the same object, then shadow apply
              if (base_z == dec_base_minus && base_z == dec_base_plus) {
                // we are in the middle of the object
              } else if (base_z == dec_base_minus && base_z != dec_base_plus) {
                // we are at the shadow side of the object
                set_fb_pixel (fb_data, origin, gpoint_null, get_light_shadow_color (get_fb_pixel (fb_data, origin, gpoint_null)));

              } else if (base_z != dec_base_minus && base_z == dec_base_plus) {
                // we are at the bright side of the object
                set_fb_pixel (fb_data, origin, gpoint_null, get_light_bright_color (get_fb_pixel (fb_data, origin, gpoint_null)));

              } else {
                // we are at an edge of the object
              }
            }
          }

          if (outer_z) {
            const GPoint translation = (GPoint) {.x = (offset_x * outer_z) / GShadowMaxValue, .y = (offset_y * outer_z) / GShadowMaxValue};
            if (gpoint_in_rect (gpoint_add (origin, translation), bounds)) {

              const GShadow dec_id_plus = (GShadow) get_fb_pixel (shadow_bitmap_data, origin, translation).argb;
              const int dec_z = (dec_id_plus != GShadowClear)? shadow_object_list [dec_id_plus & GShadowMaxRef].outer_z : outer_z;
              if (id != dec_id_plus && outer_z > dec_z) {
                // we are down the object, then shadowing occurs
                set_fb_pixel (fb_data, origin, translation, get_light_shadow_color (get_fb_pixel (fb_data, origin, translation)));
              }
            }
          }
        }
      }
    }
  } graphics_release_frame_buffer(ctx, fb);
}

////////////////////////////////////////////////////////////////////////////////
static inline uint_t row_min_x (const uint_t y) {
#if defined (PBL_RECT)
  // #pragma unused (y)
  return g_min_x;
#else
  return g_row_info [y].min_x;
#endif
}
static inline uint_t row_max_x (const uint_t y) {
#if defined (PBL_RECT)
  // #pragma unused (y)
  return g_max_x;
#else
  return g_row_info [y].max_x;
#endif
}
static inline GColor get_fb_pixel(const uint8_t *data, GPoint point, GPoint translation) {

#if defined (PBL_RECT)
  const uint_t row_beginning = (point.y + translation.y) * (g_max_x - g_min_x);
#else
  const GBitmapDataRowDelta info = g_row_info[point.y + translation.y];
  const uint_t row_beginning = info.data_delta;
#endif

#if defined (PBL_RECT)
  const uint_t row_x = point.x + translation.x;
#else
  const GBitmapDataRowDelta info_ori = g_row_info[point.y];
  const uint_t row_x = point.x + translation.x + (info.min_x - info_ori.min_x);
#endif

  return (GColor) (data [row_beginning + row_x]);
}

static inline void set_fb_pixel(uint8_t * const data, const GPoint point, const GPoint translation, const GColor color) {

#if defined (PBL_RECT)
  const uint_t row_beginning = (point.y + translation.y) * (g_max_x - g_min_x);
#else
  const GBitmapDataRowDelta info = g_row_info[point.y + translation.y];
  const uint_t row_beginning = info.data_delta;
#endif

#if defined (PBL_RECT)
  const uint_t row_x = point.x + translation.x;
#else
  const GBitmapDataRowDelta info_ori = g_row_info[point.y];
  const uint_t row_x = point.x + translation.x + (info.min_x - info_ori.min_x);
#endif

  ((GColor *) data) [row_beginning + row_x] = color;
}

GColor color_matrix [64][2]
= {
  /* GColorBlack                 ((uint8_t)0b11000000)y */  {GColorBlack, GColorDarkGray},
  /* GColorOxfordBlue            ((uint8_t)0b11000001)y */  {GColorBlack, GColorDukeBlue},
  /* GColorDukeBlue              ((uint8_t)0b11000010)y */  {GColorOxfordBlue, GColorBlue},
  /* GColorBlue                  ((uint8_t)0b11000011)y */  {GColorDukeBlue, GColorBlueMoon},
  /* GColorDarkGreen             ((uint8_t)0b11000100)y */  {GColorBlack, GColorMayGreen},
  /* GColorMidnightGreen         ((uint8_t)0b11000101)y */  {GColorOxfordBlue, GColorCadetBlue},
  /* GColorCobaltBlue            ((uint8_t)0b11000110)y */  {GColorDukeBlue, GColorVividCerulean},
  /* GColorBlueMoon              ((uint8_t)0b11000111)y */  {GColorBlue, GColorPictonBlue},
  /* GColorIslamicGreen          ((uint8_t)0b11001000)y */  {GColorDarkGreen, GColorGreen},
  /* GColorJaegerGreen           ((uint8_t)0b11001001)y */  {GColorDarkGreen, GColorMalachite},
  /* GColorTiffanyBlue           ((uint8_t)0b11001010)y */  {GColorMidnightGreen, GColorMediumSpringGreen},
  /* GColorVividCerulean         ((uint8_t)0b11001011)y */  {GColorCobaltBlue, GColorCeleste},
  /* GColorGreen                 ((uint8_t)0b11001100)y */  {GColorIslamicGreen, GColorInchworm},
  /* GColorMalachite             ((uint8_t)0b11001101)y */  {GColorJaegerGreen, GColorMintGreen},
  /* GColorMediumSpringGreen     ((uint8_t)0b11001110)y */  {GColorTiffanyBlue, GColorCeleste},
  /* GColorCyan                  ((uint8_t)0b11001111)y */  {GColorBlue, GColorCeleste},
  /* GColorBulgarianRose         ((uint8_t)0b11010000)y */  {GColorBlack, GColorDarkCandyAppleRed},
  /* GColorImperialPurple        ((uint8_t)0b11010001)y */  {GColorBlack, GColorPurple},
  /* GColorIndigo                ((uint8_t)0b11010010)y */  {GColorOxfordBlue, GColorVividViolet},
  /* GColorElectricUltramarine   ((uint8_t)0b11010011)y */  {GColorOxfordBlue, GColorBabyBlueEyes},
  /* GColorArmyGreen             ((uint8_t)0b11010100)y */  {GColorBlack, GColorBrass},
  /* GColorDarkGray              ((uint8_t)0b11010101)y */  {GColorBlack, GColorLightGray},
  /* GColorLiberty               ((uint8_t)0b11010110)y */  {GColorOxfordBlue, GColorBabyBlueEyes},
  /* GColorVeryLightBlue         ((uint8_t)0b11010111)y */  {GColorDukeBlue, GColorBabyBlueEyes},
  /* GColorKellyGreen            ((uint8_t)0b11011000)y */  {GColorDarkGreen, GColorSpringBud},
  /* GColorMayGreen              ((uint8_t)0b11011001)y */  {GColorMidnightGreen, GColorMalachite},
  /* GColorCadetBlue             ((uint8_t)0b11011010)y */  {GColorMidnightGreen, GColorMediumSpringGreen},
  /* GColorPictonBlue            ((uint8_t)0b11011011)y */  {GColorCobaltBlue, GColorCeleste},
  /* GColorBrightGreen           ((uint8_t)0b11011100)y */  {GColorKellyGreen, GColorMintGreen},
  /* GColorScreaminGreen         ((uint8_t)0b11011101)y */  {GColorGreen, GColorMintGreen},
  /* GColorMediumAquamarine      ((uint8_t)0b11011110)y */  {GColorJaegerGreen, GColorCeleste},
  /* GColorElectricBlue          ((uint8_t)0b11011111)y */  {GColorTiffanyBlue, GColorCeleste},
  /* GColorDarkCandyAppleRed     ((uint8_t)0b11100000)y */  {GColorBulgarianRose, GColorSunsetOrange},
  /* GColorJazzberryJam          ((uint8_t)0b11100001)y */  {GColorImperialPurple, GColorMagenta},
  /* GColorPurple                ((uint8_t)0b11100010)y */  {GColorImperialPurple, GColorShockingPink},
  /* GColorVividViolet           ((uint8_t)0b11100011)y */  {GColorImperialPurple, GColorBabyBlueEyes},
  /* GColorWindsorTan            ((uint8_t)0b11100100)y */  {GColorBlack, GColorRajah},
  /* GColorRoseVale              ((uint8_t)0b11100101)y */  {GColorBulgarianRose, GColorMelon},
  /* GColorPurpureus             ((uint8_t)0b11100110)y */  {GColorPurple, GColorRichBrilliantLavender},
  /* GColorLavenderIndigo        ((uint8_t)0b11100111)y */  {GColorPurple, GColorRichBrilliantLavender},
  /* GColorLimerick              ((uint8_t)0b11101000)y */  {GColorArmyGreen, GColorSpringBud},
  /* GColorBrass                 ((uint8_t)0b11101001)y */  {GColorArmyGreen, GColorSpringBud},
  /* GColorLightGray             ((uint8_t)0b11101010)y */  {GColorDarkGray, GColorWhite},
  /* GColorBabyBlueEyes          ((uint8_t)0b11101011)y */  {GColorElectricUltramarine, GColorWhite},
  /* GColorSpringBud             ((uint8_t)0b11101100)y */  {GColorKellyGreen, GColorWhite},
  /* GColorInchworm              ((uint8_t)0b11101101)y */  {GColorSpringBud, GColorWhite},
  /* GColorMintGreen             ((uint8_t)0b11101110)y */  {GColorScreaminGreen, GColorWhite},
  /* GColorCeleste               ((uint8_t)0b11101111)y */  {GColorElectricBlue, GColorWhite},
  /* GColorRed                   ((uint8_t)0b11110000)y */  {GColorDarkCandyAppleRed, GColorSunsetOrange},
  /* GColorFolly                 ((uint8_t)0b11110001)y */  {GColorRoseVale, GColorBrilliantRose},
  /* GColorFashionMagenta        ((uint8_t)0b11110010)y */  {GColorJazzberryJam, GColorBrilliantRose},
  /* GColorMagenta               ((uint8_t)0b11110011)y */  {GColorJazzberryJam, GColorRichBrilliantLavender},
  /* GColorOrange                ((uint8_t)0b11110100)y */  {GColorWindsorTan, GColorRajah},
  /* GColorSunsetOrange          ((uint8_t)0b11110101)y */  {GColorRed, GColorMelon},
  /* GColorBrilliantRose         ((uint8_t)0b11110110)y */  {GColorFashionMagenta, GColorRichBrilliantLavender},
  /* GColorShockingPink          ((uint8_t)0b11110111)y */  {GColorJazzberryJam, GColorRichBrilliantLavender},
  /* GColorChromeYellow          ((uint8_t)0b11111000)y */  {GColorWindsorTan, GColorYellow},
  /* GColorRajah                 ((uint8_t)0b11111001)y */  {GColorChromeYellow, GColorIcterine},
  /* GColorMelon                 ((uint8_t)0b11111010)y */  {GColorSunsetOrange, GColorWhite},
  /* GColorRichBrilliantLavender ((uint8_t)0b11111011)y */  {GColorMagenta, GColorWhite},
  /* GColorYellow                ((uint8_t)0b11111100)y */  {GColorChromeYellow, GColorYellow},
  /* GColorIcterine              ((uint8_t)0b11111101)y */  {GColorYellow, GColorPastelYellow},
  /* GColorPastelYellow          ((uint8_t)0b11111110)y */  {GColorIcterine, GColorWhite},
  /* GColorWhite                 ((uint8_t)0b11111111)y */  {GColorLightGray, GColorWhite}
};

inline GColor get_light_shadow_color (const GColor c) {
  return color_matrix [c.argb & 0b00111111][0];
}

GColor get_light_bright_color (const GColor c) {
  return color_matrix [c.argb & 0b00111111][1];
}

void test_shadow_layer_proc (Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  for (int i = 0; i < 64; i++) {
    int x = i % 4; int y = i / 4;
    int w = bounds.size.w / 4; int h = bounds.size.h / (64 / 4);
    GColor c = (GColor){.argb = (0b11<<6)+i};
    GColor r = get_light_shadow_color (c);
    GColor s = get_light_bright_color (c);
    graphics_context_set_fill_color(ctx, r);
    graphics_fill_rect (ctx, GRect (x * w, y * h, w / 3, h), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, c);
    graphics_fill_rect (ctx, GRect (x * w + w / 3, y * h, w / 3, h), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, s);
    graphics_fill_rect (ctx, GRect (x * w + 2 * w / 3, y * h, w / 3, h), 0, GCornerNone);
  }
}
