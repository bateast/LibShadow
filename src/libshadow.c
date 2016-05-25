#include <pebble.h>

#include "libshadow.h"

struct ShadowBitmap
{
  GBitmap *bitmap;
  size_t data_size;
  GColor mask_color;
};

void set_fb_pixel(GBitmap *bitmap, int y, int x, GColor color);
GColor get_fb_pixel(GBitmap *bitmap, int y, int x);

GColor get_light_shadow_color (GColor c);
GColor get_light_bright_color (GColor c);

////////////////////////////////////////////////////////////////////////////////

static int shadow_object_list [GShadowMaxRef][2];
static GShadow shadow_object_current = 0;

static GBitmap *shadow_bitmap = NULL;
static uint8_t *shadow_bitmap_data;
static size_t shadow_bitmap_size;
static GRect shadow_bitmap_bounds;
static uint16_t shadow_bitmap_bytes_per_row;
static GBitmapFormat shadow_bitmap_format;

static uint8_t *fb_data;

GColor8 gcolor (GShadow shadow) {
  return (GColor8) {.argb = (uint8_t) shadow};
}

GShadow new_shadowing_object (int inner_z, int outer_z) {
  GShadow r = shadow_object_current;
  shadow_object_list [shadow_object_current][0] = inner_z;
  shadow_object_list [shadow_object_current][1] =  outer_z;
  shadow_object_current = (shadow_object_current + 1) % GShadowMaxRef;

  return GShadowUnclear | r;
}

void reset_shadow () {
  memset (shadow_bitmap_data, GShadowClear, shadow_bitmap_size);
}

void switch_to_shadow_ctx (GContext *ctx) {
  GBitmap *fb = graphics_capture_frame_buffer(ctx); {
    if (shadow_bitmap == NULL) {
      shadow_bitmap_format = gbitmap_get_format (fb);
      shadow_bitmap_bounds = gbitmap_get_bounds (fb);
      shadow_bitmap_bytes_per_row = gbitmap_get_bytes_per_row (fb);
      shadow_bitmap_size = gbitmap_get_bytes_per_row(fb) * gbitmap_get_bounds(fb).size.h;

      shadow_bitmap_data = malloc (shadow_bitmap_size);
      memset (shadow_bitmap_data, GShadowClear, shadow_bitmap_size);

      shadow_bitmap = gbitmap_create_with_data (shadow_bitmap_data);
      gbitmap_set_data (shadow_bitmap, shadow_bitmap_data, shadow_bitmap_format, shadow_bitmap_bytes_per_row,true);
      gbitmap_set_bounds (shadow_bitmap, shadow_bitmap_bounds);
    }

    fb_data = gbitmap_get_data (fb);
    gbitmap_set_data (fb, shadow_bitmap_data, shadow_bitmap_format, shadow_bitmap_bytes_per_row, true);

  } graphics_release_frame_buffer(ctx, fb);
}

void revert_to_fb_ctx (GContext *ctx) {
  GBitmap *fb = graphics_capture_frame_buffer(ctx); {
    gbitmap_set_data (fb, fb_data, shadow_bitmap_format, shadow_bitmap_bytes_per_row, true);
  } graphics_release_frame_buffer(ctx, fb);
}

void destroy_shadow_ctx () {
  gbitmap_destroy (shadow_bitmap);
};

void create_shadow (GContext *ctx, int32_t angle) {
  // compute x and y offset from angle and height (z)
  const int offset_x = (- cos_lookup (angle) * GShadowMaxValue * 2) / TRIG_MAX_RATIO;
  const int offset_y = (sin_lookup (angle) * GShadowMaxValue * 2) / TRIG_MAX_RATIO;

  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  {
    // Manipulate the image data...
    const GRect bounds = gbitmap_get_bounds(fb);
    // Iterate over all rows
    for(int y = bounds.origin.y; y < bounds.origin.y + bounds.size.h; y++) {
      for(int x = bounds.origin.x; x < bounds.origin.x + bounds.size.w; x++) {
        const GShadow id = (GShadow)get_fb_pixel (shadow_bitmap, y, x).argb;
        if (id != GShadowClear) {
          const GShadow inner_z = shadow_object_list [id & GShadowMaxRef][0];
          const GShadow outer_z = shadow_object_list [id & GShadowMaxRef][1];
          if (inner_z) {
            const int inner_x_plus = x + (offset_x * inner_z) / GShadowMaxValue;
            const int inner_y_plus = y + (offset_y * inner_z) / GShadowMaxValue;
            const int inner_x_minus = x - (offset_x * inner_z) / GShadowMaxValue;
            const int inner_y_minus = y - (offset_y * inner_z) / GShadowMaxValue;

            if (bounds.origin.y <= inner_y_plus  && inner_y_plus  < bounds.origin.y + bounds.size.h &&
                bounds.origin.x <= inner_x_plus  && inner_x_plus  < bounds.origin.x + bounds.size.w &&
                bounds.origin.y <= inner_y_minus && inner_y_minus < bounds.origin.y + bounds.size.h &&
                bounds.origin.x <= inner_x_minus && inner_x_minus < bounds.origin.x + bounds.size.w) {
              const int dec_id_plus  = (GShadow) get_fb_pixel (shadow_bitmap, inner_y_plus, inner_x_plus).argb;
              const int dec_id_minus = (GShadow) get_fb_pixel (shadow_bitmap, inner_y_minus, inner_x_minus).argb;
              // we are still on the same object, then shadow apply
              if (id == dec_id_minus && id == dec_id_plus) {
                // we are in the middle of the object
              } else if (id == dec_id_minus && id != dec_id_plus) {
                // we are at the shadow side of the object
                set_fb_pixel (fb, y, x, get_light_shadow_color (get_fb_pixel (fb, y, x)));
              } else if (id != dec_id_minus && id == dec_id_plus) {
                // we are at the bright side of the object
                set_fb_pixel (fb, y, x, get_light_bright_color (get_fb_pixel (fb, y, x)));
              } else {
                // we are at an edge of the object
              }
            }
          }
          if (outer_z) {
            const int outer_x = x + (offset_x * outer_z) / GShadowMaxValue;
            const int outer_y = y + (offset_y * outer_z) / GShadowMaxValue;
            if (bounds.origin.y <= outer_y  && outer_y  < bounds.origin.y + bounds.size.h &&
                bounds.origin.x <= outer_x  && outer_x  < bounds.origin.x + bounds.size.w ) {
              const GShadow dec_id_plus = (GShadow) get_fb_pixel (shadow_bitmap, outer_y, outer_x).argb;
              const int dec_z = (dec_id_plus != GShadowClear)? shadow_object_list [dec_id_plus][1] : 0;
              if (id != dec_id_plus &&
                  (outer_z == GShadowClear || outer_z > dec_z)) {
                // we are down the object, then shadowing occurs
                set_fb_pixel (fb, outer_y, outer_x, get_light_shadow_color (get_fb_pixel (fb, outer_y, outer_x)));
              }
            }
          }
        }
      }
    }
  } graphics_release_frame_buffer(ctx, fb);
}

////////////////////////////////////////////////////////////////////////////////


// get pixel color at given coordinates
GColor get_fb_pixel(GBitmap *bitmap, int y, int x) {
  static uint16_t byte_per_row;
  static bool first_run = true;
  const uint8_t *data = gbitmap_get_data (bitmap);

  if (first_run) {
    byte_per_row = gbitmap_get_bytes_per_row (bitmap);
    first_run = false;
  }

#if (defined(PBL_BW) && defined(PLB_RECT))
  return ((data[y*byte_per_row + x / 8] >> (x % 8)) & 1) == 1? (GColor)GColorWhiteARGB8: (GColor)GColorBlackARGB8;
#endif
#if (defined (PBL_COLOR) && defined (PBL_ROUND))
  const GBitmapDataRowInfo info = gbitmap_get_data_row_info(bitmap, y);
  if ((x >= info.min_x) && (x <= info.max_x))
    return (GColor)info.data[x];
  else
    return GColorClear;
#endif
  /* #if (defined (PBL_COLOR) && defined (PBL_RECT)) */
  return (GColor)data[y*byte_per_row + x];
  /* #endif */
}

// set pixel color at given coordinates
void set_fb_pixel(GBitmap *bitmap, int y, int x, GColor color) {
  static uint16_t byte_per_row;
  static bool first_run = true;
  uint8_t *data = gbitmap_get_data (bitmap);

  if (first_run) {
    byte_per_row = gbitmap_get_bytes_per_row (bitmap);
    first_run = false;
  }

#if (defined(PBL_BW) && defined(PLB_RECT))
  const uint8_t bit = (gcolor_equal (color, (GColor)GColorWhiteARGB8)? 1: 0); // YG 2016-05-09: SDK3 update - White Color means 1 in 1bit bitmapsm; Black Means 0
  data[y*byte_per_row + x / 8] ^= (-bit ^ data[y*byte_per_row + x / 8]) & (1 << (x % 8));

#endif
#if (defined (PBL_COLOR) && defined (PBL_ROUND))
  const GBitmapDataRowInfo info = gbitmap_get_data_row_info(bitmap, y);
  if ((x >= info.min_x) && (x <= info.max_x)) {
    ((GColor *)info.data)[x] = color;
  }
#endif
  /* #if (defined (PBL_COLOR) && defined (PBL_RECT)) */
  ((GColor *)data)[y*byte_per_row + x] = color;
  /* #endif */
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

inline GColor get_light_shadow_color (GColor c) {
  return color_matrix [c.argb & 0b00111111][0];
}

GColor get_light_bright_color (GColor c) {
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
