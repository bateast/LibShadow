/*
 * My Tic-tok
 *
 * Copyright (c) 2016 James Fowler
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <pebble.h>
#include "shadowed-face.h"
#include "libshadow.h"

#define ANTIALIASING true

#define TOP_BLOB_SIZE       5
#define MINUTE_HAND_MARGIN  16
#define HOUR_HAND_MARGIN    42

#define ANIMATION_DURATION  3000
#define ANIMATION_DELAY     0

#define BACKGROUND_COLOUR   PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack)
#define MINUTE_HAND_COLOR   PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite)
#define TOP_BLOB_COLOUR     PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite)

#define GColorShadow GColorMelon

#define WH_WIDTH            9

typedef struct {
  int hours;
  int minutes;
} Time;

static Window *window;
static Layer *background_layer;
static Layer *hands_layer;

static GPoint screen_centre;
static Time s_last_time;
static int animpercent = 0;
static bool are_we_animating = true;
static bool bt_on = false;
static GColor hour_colour;
static GShadow minute_shadow;
static GShadow hour_shadow;
static GShadow dot_shadow;

/*
 * Animation start
 */
static void animation_started(Animation *anim, void *context) {
  are_we_animating = true;
  if (background_layer) {
    layer_mark_dirty(background_layer);
  }
}

/*
 * Animation end
 */
static void animation_stopped(Animation *anim, bool stopped, void *context) {
  are_we_animating = false;
  if (background_layer) {
    layer_mark_dirty(background_layer);
  }
}

/*
 * Define animation
 */
static void animate(int duration, int delay, AnimationImplementation *implementation) {
  Animation *anim = animation_create();
  animation_set_duration(anim, duration);
  animation_set_delay(anim, delay);
  animation_set_curve(anim, AnimationCurveEaseInOut);
  animation_set_implementation(anim, implementation);
  animation_set_handlers(anim, (AnimationHandlers ) { .started = animation_started, .stopped = animation_stopped }, NULL);
  animation_schedule(anim);
}

/*
 * Clock tick processing
 */
static void tick_handler(struct tm *tick_time, TimeUnits changed) {
  // Store time
  s_last_time.hours = tick_time->tm_hour;
  s_last_time.hours -= (s_last_time.hours > 12) ? 12 : 0;
  s_last_time.minutes = tick_time->tm_min;

  // Redraw
  if (hands_layer) {
    layer_mark_dirty(hands_layer);
  }
}

/*
 * Bluetooth on off handling
 */
static void handle_bluetooth(bool connected) {
  if (connected != bt_on) {
    vibes_short_pulse();
    // Want to hide the top dot if not in bt range
    bt_on = connected;
    // Redraw
    if (background_layer) {
      layer_mark_dirty(background_layer);
    }
  }
}

/*
 * Hour hand colour based on battery deadness
 */
static void handle_battery(BatteryChargeState charge) {
#ifdef PBL_BW
  hour_colour = GColorWhite;
#else

  int red = (100 - charge.charge_percent) * 255 / 100;
  int green = charge.charge_percent * 255 / 100;
  int blue = 128;
  hour_colour = GColorFromRGB(red, green, blue);

  // Redraw
  if (hands_layer) {
    layer_mark_dirty(hands_layer);
  }
#endif
}

/*
 * Calculate minute angle for minute hand
 */
static int32_t get_angle_for_minute(int minute) {
  // Progress through 60 minutes, out of 360 degrees
  return ((minute * 360) / 60);
}

/*
 * Calculate hour angle for hour hand
 */
static int32_t get_angle_for_hour(int hour, int minute) {
  // Progress through 12 hours, out of 360 degrees
  return (((hour * 360) / 12) + (get_angle_for_minute(minute) / 12));
}

/*
 * Set up the background, complete with 12 o'clock blobby
 * shown only after animation complete and only if bluetooth is available
 */
static void background_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, BACKGROUND_COLOUR);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_antialiased(ctx, ANTIALIASING);

  if (bt_on && !are_we_animating) {
    GRect insetbounds = grect_inset(bounds, GEdgeInsets(2));
    GPoint pos = gpoint_from_polar(insetbounds, GOvalScaleModeFitCircle, DEG_TO_TRIGANGLE(0));
    graphics_context_set_fill_color(ctx, TOP_BLOB_COLOUR);
    graphics_fill_circle(ctx, pos, TOP_BLOB_SIZE);

    switch_to_shadow_ctx (ctx);{
      graphics_context_set_antialiased(ctx, false);
      graphics_context_set_fill_color(ctx, gcolor (dot_shadow));
      graphics_fill_circle(ctx, pos, TOP_BLOB_SIZE);
    }revert_to_fb_ctx (ctx);
  }
}

/*
 * Put hands on the watch
 */
static void hands_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GRect bounds_h = bounds;
  bounds_h.size.w = bounds_h.size.h;
  bounds_h.origin.x -= (bounds_h.size.w - bounds.size.w) / 2;
  int maxradius = bounds_h.size.w;
  if (bounds_h.size.h < maxradius) {
    maxradius = bounds_h.size.h;
  }
  maxradius /= 2;
  int animradius = maxradius - ((maxradius * animpercent) / 100);
#ifdef PBL_RECT
  int platform_margin_m = (MINUTE_HAND_MARGIN/0.9);
#else
  int platform_margin_m = MINUTE_HAND_MARGIN;
#endif
  int outer_m = animradius + platform_margin_m;
  int outer_h = animradius + HOUR_HAND_MARGIN;

  if (outer_m < platform_margin_m) {
    outer_m = platform_margin_m;
  }
  if (outer_h < HOUR_HAND_MARGIN) {
    outer_h = HOUR_HAND_MARGIN;
  }
  if (outer_m > maxradius) {
    outer_m = maxradius;
  }
  if (outer_h > maxradius) {
    outer_h = maxradius;
  }
  GRect bounds_mo = grect_inset(bounds_h, GEdgeInsets(outer_m));
  GRect bounds_ho = grect_inset(bounds_h, GEdgeInsets(outer_h));
  graphics_context_set_antialiased(ctx, ANTIALIASING);

  // Use current time while animating
  Time mode_time = s_last_time;

  // Adjust for minutes through the hour
  float hour_deg = get_angle_for_hour(mode_time.hours, mode_time.minutes);
  float minute_deg = get_angle_for_minute(mode_time.minutes);

  GPoint minute_hand_outer = gpoint_from_polar(bounds_mo, GOvalScaleModeFillCircle, DEG_TO_TRIGANGLE(minute_deg));
  GPoint hour_hand_outer = gpoint_from_polar(bounds_ho, GOvalScaleModeFillCircle, DEG_TO_TRIGANGLE(hour_deg));

  graphics_context_set_antialiased(ctx, ANTIALIASING);
  graphics_context_set_stroke_width(ctx, WH_WIDTH);

  graphics_context_set_stroke_color(ctx, MINUTE_HAND_COLOR);
  graphics_draw_line(ctx, screen_centre, minute_hand_outer);

  graphics_context_set_stroke_color(ctx, hour_colour);
  graphics_draw_line(ctx, screen_centre, hour_hand_outer);

  switch_to_shadow_ctx (ctx);{
    graphics_context_set_antialiased(ctx, false);

    graphics_context_set_stroke_color(ctx, gcolor (minute_shadow));
    graphics_draw_line(ctx, screen_centre, minute_hand_outer);

    graphics_context_set_stroke_color(ctx, gcolor (hour_shadow));
    graphics_draw_line(ctx, screen_centre, hour_hand_outer);
  }revert_to_fb_ctx (ctx);

  create_shadow (ctx, NW);
  reset_shadow ();
}

/*
 * Load the window
 */
static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect window_bounds = layer_get_bounds(window_layer);

  screen_centre = grect_center_point(&window_bounds);
  screen_centre.x -= 1;
  screen_centre.y -= 1;

  background_layer = layer_create(window_bounds);
  hands_layer = layer_create(window_bounds);
  layer_set_update_proc(background_layer, background_update_proc);
  layer_set_update_proc(hands_layer, hands_update_proc);
  layer_add_child(window_layer, background_layer);
  layer_add_child(background_layer, hands_layer);

  hour_shadow = new_shadowing_object (2, 8);
  minute_shadow = new_shadowing_object (2, 4);
  dot_shadow = new_shadowing_object (-2, 0);
}

/*
 * Unload the window
 */
static void window_unload(Window *window) {
  layer_destroy(background_layer);
  layer_destroy(hands_layer);
}

/*
 * Calculate anim percentage
 */
static int anim_percentage(AnimationProgress dist_normalized, int max) {
  return (int) (float) (((float) dist_normalized / (float) ANIMATION_NORMALIZED_MAX) * (float) max);
}

/*
 * Update the radias
 */
static void radius_update(Animation *anim, AnimationProgress dist_normalized) {
  animpercent = anim_percentage(dist_normalized, 100);
  layer_mark_dirty(hands_layer);
}

/*
 * Initialisation
 */
static void init() {
  srand(time(NULL));

  time_t t = time(NULL);
  struct tm *time_now = localtime(&t);
  tick_handler(time_now, MINUTE_UNIT);

  window = window_create();
  window_set_window_handlers(window, (WindowHandlers ) { .load = window_load, .unload = window_unload, });
  window_stack_push(window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

  bt_on = connection_service_peek_pebble_app_connection();
  handle_bluetooth(bt_on);
  connection_service_subscribe((ConnectionHandlers ) { .pebble_app_connection_handler = handle_bluetooth });

  handle_battery(battery_state_service_peek());
  battery_state_service_subscribe(handle_battery);

  // Prepare animations
  AnimationImplementation radius_impl = { .update = radius_update };
  animate(ANIMATION_DURATION, ANIMATION_DELAY, &radius_impl);
}

/*
 * Closedown
 */
static void deinit() {
  window_destroy(window);
}

/*
 * Main processing
 */
int main() {
  init();
  app_event_loop();
  deinit();
}
