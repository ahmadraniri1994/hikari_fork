#if !defined(HIKARI_BORDER_H)
#define HIKARI_BORDER_H

#include <stdbool.h>

#include <wlr/util/box.h>

#include <hikari/output.h>

struct hikari_renderer;
struct wlr_texture;

enum hikari_border_state {
  HIKARI_BORDER_NONE,
  HIKARI_BORDER_INACTIVE,
  HIKARI_BORDER_ACTIVE
};

struct hikari_border {
  enum hikari_border_state state;

  struct wlr_box geometry;
  struct wlr_box top;
  struct wlr_box bottom;
  struct wlr_box left;
  struct wlr_box right;
  struct wlr_box corner_nw;
  struct wlr_box corner_ne;
  struct wlr_box corner_sw;
  struct wlr_box corner_se;

  /*
   * Per-view frame texture for styled borders (FVWM or MWM).
   * Contains the full frame ring rendered with concentric relief
   * rectangles and handle marks.  The texture is frame_w x frame_h
   * pixels (scaled) with a transparent center.
   */
  struct wlr_texture *frame_texture;
  int frame_w;       /* cached scaled frame width */
  int frame_h;       /* cached scaled frame height */
  float frame_scale; /* cached output scale */
  bool frame_dirty;  /* true if texture needs regeneration */
  enum hikari_border_state frame_generated_state; /* state last generated */
};

struct wlr_box *
hikari_border_geometry(struct hikari_border *border);

void
hikari_border_refresh_geometry(
    struct hikari_border *border, struct wlr_box *geometry);

#endif
