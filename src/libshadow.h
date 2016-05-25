#pragma once

#include <pebble.h>
struct ShadowBitmap;
typedef struct ShadowBitmap ShadowBitmap;

#define NW (((TRIG_MAX_ANGLE) * 3) / 8)

typedef int8_t GShadow;
#define GShadowClear     ((GShadow) 0b00000000)
#define GShadowMaxRef    ((GShadow) 0b00111111)
#define GShadowUnclear   (~ GShadowMaxRef)
#define GShadowMaxValue  10000
GColor gcolor (GShadow shadow);

GShadow new_shadowing_object (int inner_z, int outer_z);

void switch_to_shadow_ctx (GContext *ctx);
void revert_to_fb_ctx (GContext *ctx);
void destroy_shadow_ctx ();
// The angle value is scaled linearly, such that a value of 0x10000 corresponds to 360 degrees or 2 PI radians.
void create_shadow (GContext *ctx, int32_t angle);
void reset_shadow ();

void test_shadow_layer_proc (Layer *layer, GContext *ctx);
