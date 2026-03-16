#if !defined(HIKARI_BORDER_STYLE_H)
#define HIKARI_BORDER_STYLE_H


#include <stdbool.h>

#include <wlr/render/wlr_renderer.h>

struct hikari_border;
struct hikari_border_color;

/*
 * Generate a per-view border frame texture for FVWM or MWM style.
 *
 * Renders the full window frame ring (frame_w x frame_h scaled pixels)
 * using concentric relief rectangles matching fvwm3's algorithm.
 *
 * FVWM style: 7-layer relief (dout/hiout/trout/center/trin/shin/din),
 * handle marks with 2px thickness.
 *
 * MWM style: 3-layer relief (2px hiout, center, 1px shin),
 * handle marks with 1px thickness.
 *
 * Returns true on success.
 */
bool
hikari_border_frame_generate(struct hikari_border *border,
    struct wlr_renderer *renderer,
    const struct hikari_border_color *colors,
    int border_width,
    int corner_length,
    float scale,
    int style);

/*
 * Free the per-view border frame texture.
 */
void
hikari_border_frame_fini(struct hikari_border *border);

#endif
