#include <hikari/border.h>

#include <assert.h>

#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_output.h>

#include <hikari/border_style.h>
#include <hikari/color.h>
#include <hikari/configuration.h>
#include <hikari/node.h>
#include <hikari/output.h>
#include <hikari/renderer.h>

inline struct wlr_box *
hikari_border_geometry(struct hikari_border *border)
{
  return &border->geometry;
}

void
hikari_border_refresh_geometry(
    struct hikari_border *border, struct wlr_box *geometry)
{
  if (border->state == HIKARI_BORDER_NONE) {
    border->geometry = *geometry;
    return;
  }

  int border_width = hikari_configuration->border;

  /*
   * Corner length: config corner_length + border_width, matching
   * fvwm3's formula (title_thickness + boundary_width).
   */
  int corner_length = hikari_configuration->corner_length + border_width;

  border->geometry.x = geometry->x - border_width;
  border->geometry.y = geometry->y - border_width;
  border->geometry.width = geometry->width + border_width * 2;
  border->geometry.height = geometry->height + border_width * 2;

  int bx = border->geometry.x;
  int by = border->geometry.y;
  int bw = border->geometry.width;
  int bh = border->geometry.height;

  /* Clamp corner_length if frame is too small */
  int min_l = 2 * corner_length + 4;
  if (bw < min_l) {
    corner_length = bw / 3;
  }
  if (bh < min_l) {
    corner_length = (bh / 3 < corner_length) ? bh / 3 : corner_length;
  }
  if (corner_length < border_width) {
    corner_length = border_width;
  }

  /* Side bars: between the corners, border_width thick */
  border->top.x = bx + corner_length;
  border->top.y = by;
  border->top.width = bw - corner_length * 2;
  border->top.height = border_width;

  border->bottom.x = bx + corner_length;
  border->bottom.y = by + bh - border_width;
  border->bottom.width = bw - corner_length * 2;
  border->bottom.height = border_width;

  border->left.x = bx;
  border->left.y = by + corner_length;
  border->left.width = border_width;
  border->left.height = bh - corner_length * 2;

  border->right.x = bx + bw - border_width;
  border->right.y = by + corner_length;
  border->right.width = border_width;
  border->right.height = bh - corner_length * 2;

  /* Corner handles: corner_length x corner_length */
  border->corner_nw.x = bx;
  border->corner_nw.y = by;
  border->corner_nw.width = corner_length;
  border->corner_nw.height = corner_length;

  border->corner_ne.x = bx + bw - corner_length;
  border->corner_ne.y = by;
  border->corner_ne.width = corner_length;
  border->corner_ne.height = corner_length;

  border->corner_sw.x = bx;
  border->corner_sw.y = by + bh - corner_length;
  border->corner_sw.width = corner_length;
  border->corner_sw.height = corner_length;

  border->corner_se.x = bx + bw - corner_length;
  border->corner_se.y = by + bh - corner_length;
  border->corner_se.width = corner_length;
  border->corner_se.height = corner_length;

  /*
   * Mark the per-view border frame texture as dirty so it will be
   * regenerated at render time (lazy generation avoids redundant work
   * during rapid interactive resizes).
   */
  if (hikari_configuration->border_style != HIKARI_BORDER_STYLE_NONE) {
    border->frame_dirty = true;
  }
}
