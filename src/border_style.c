
#include <hikari/border_style.h>

#include <stdlib.h>
#include <string.h>

#include <drm_fourcc.h>
#include <pixman.h>

#include <hikari/border.h>
#include <hikari/color.h>
#include <hikari/configuration.h>

/* Convert float[4] RGBA to packed ARGB8888 pixel. */
#define F2PX(c)                                                                \
  (((uint32_t)((c)[3] * 255) << 24) | ((uint32_t)((c)[0] * 255) << 16) |       \
      ((uint32_t)((c)[1] * 255) << 8) | (uint32_t)((c)[2] * 255))

/* Sentinel: transparent / skip drawing. */
#define TRANSPARENT_PX 0xFFFFFFFF

void
hikari_border_frame_fini(struct hikari_border *border)
{
  if (border->frame_texture != NULL) {
    wlr_texture_destroy(border->frame_texture);
    border->frame_texture = NULL;
  }
  border->frame_w = 0;
  border->frame_h = 0;
  border->frame_scale = 0;
}

static inline void
hline(uint32_t *pixels,
    int stride,
    int img_w,
    int img_h,
    int x1,
    int x2,
    int y,
    uint32_t color)
{
  if (y < 0 || y >= img_h)
    return;
  if (x1 < 0)
    x1 = 0;
  if (x2 >= img_w)
    x2 = img_w - 1;
  for (int x = x1; x <= x2; x++)
    pixels[y * stride + x] = color;
}

static inline void
vline(uint32_t *pixels,
    int stride,
    int img_w,
    int img_h,
    int x,
    int y1,
    int y2,
    uint32_t color)
{
  if (x < 0 || x >= img_w)
    return;
  if (y1 < 0)
    y1 = 0;
  if (y2 >= img_h)
    y2 = img_h - 1;
  for (int y = y1; y <= y2; y++)
    pixels[y * stride + x] = color;
}

/*
 * Draws `line_width` concentric rectangles of a frame from (x,y) with
 * dimensions w x h.  Left+top are drawn first (UL batch), then
 * bottom+right (BR batch), matching fvwm's corner pixel ownership.
 *
 * With per-edge colors, each segment in the UL batch uses its own
 * edge-specific color (left uses west, top uses north), and similarly
 * for the BR batch (bottom uses south, right uses east).
 *
 * ul_left/ul_top:  colors for left/top edges (from ulgc table)
 * br_bot/br_right: colors for bottom/right edges (from brgc table)
 *
 * A role value of 0xFFFFFFFF means "transparent" (skip drawing),
 * matching fvwm3's GXnoop transparent GC.
 */

static void
relieve_rect_per_edge(uint32_t *pixels,
    int stride,
    int img_w,
    int img_h,
    int x,
    int y,
    int w,
    int h,
    int line_width,
    uint32_t ul_left,
    uint32_t ul_top,
    uint32_t br_bot,
    uint32_t br_right)
{
  int max_w, max_h;

  if (w <= 0 || h <= 0 || line_width <= 0)
    return;

  max_w = (w + 1) / 2;
  if (max_w > line_width)
    max_w = line_width;
  max_h = (h + 1) / 2;
  if (max_h > line_width)
    max_h = line_width;

  /*
   * UL batch: left edge then top edge.
   *
   * Matches fvwm's do_relieve_rectangle.
   *
   *   Left:   (x+i, y+i)      to (x+i,     y+h-1-i)    [a=0]
   *   Top:    (x+i+1, y+i)    to (x+w-1-i+1, y+i)      [l=1, so +1 on x]
   *   Bottom: (x+i, y+h-i)    to (x+w-1-i, y+h-i)      [a=0]
   *   Right:  (x+w-i, y+i+1)  to (x+w-i,   y+h-1-i+1)  [l=1, so +1 on y]
   */
  for (int i = 0; i < max_w; i++) {
    /* Left edge: vertical from (x+i, y+i) to (x+i, y+h-1-i) */
    if (ul_left != TRANSPARENT_PX)
      vline(pixels, stride, img_w, img_h, x + i, y + i, y + h - 1 - i, ul_left);
  }
  for (int i = 0; i < max_h; i++) {
    /* Top edge: horizontal from (x+i+1, y+i) to (x+w-i, y+i) */
    if (ul_top != TRANSPARENT_PX)
      hline(pixels, stride, img_w, img_h, x + i + 1, x + w - i, y + i, ul_top);
  }

  /*
   * BR batch: bottom edge then right edge.
   */
  for (int i = 0; i < max_h; i++) {
    /* Bottom edge: horizontal from (x+i, y+h-i) to (x+w-1-i, y+h-i) */
    if (br_bot != TRANSPARENT_PX)
      hline(pixels,
          stride,
          img_w,
          img_h,
          x + i,
          x + w - 1 - i,
          y + h - i,
          br_bot);
  }
  for (int i = 0; i < max_w; i++) {
    /* Right edge: vertical from (x+w-i, y+i+1) to (x+w-i, y+h-i) */
    if (br_right != TRANSPARENT_PX)
      vline(pixels,
          stride,
          img_w,
          img_h,
          x + w - i,
          y + i + 1,
          y + h - i,
          br_right);
  }
}

/*
 * FVWM x_mark equivalent: draws horizontal lines forming a triangular
 * wedge at a handle mark position.
 *
 * `offset_tl` is added to x before drawing (w_dout offset).
 * When do_draw_shadow is true, lines extend upward (shadow mark).
 * When false, lines extend downward and shift left (relief mark).
 */
static void
draw_x_mark(uint32_t *pixels,
    int stride,
    int img_w,
    int img_h,
    int x,
    int y,
    int mark_length,
    int mark_thickness,
    int offset_tl,
    bool do_draw_shadow,
    uint32_t color)
{
  x += offset_tl;
  for (int k = 0; k < mark_thickness; k++) {
    int len = mark_length - 1 - k;
    if (len < 0)
      break;
    int x1, y1;
    if (do_draw_shadow) {
      x1 = x;
      y1 = y - 1 - k;
    } else {
      x1 = x - k;
      y1 = y + k;
    }
    hline(pixels, stride, img_w, img_h, x1, x1 + len, y1, color);
  }
}

/*
 * FVWM y_mark equivalent: draws vertical lines forming a triangular
 * wedge at a handle mark position.
 *
 * `offset_tl` is added to y before drawing.
 */
static void
draw_y_mark(uint32_t *pixels,
    int stride,
    int img_w,
    int img_h,
    int x,
    int y,
    int mark_length,
    int mark_thickness,
    int offset_tl,
    bool do_draw_shadow,
    uint32_t color)
{
  y += offset_tl;
  for (int k = 0; k < mark_thickness; k++) {
    int len = mark_length - k;
    if (len <= 0)
      break;
    int x1, y1;
    if (do_draw_shadow) {
      x1 = x - 1 - k;
      y1 = y + k;
    } else {
      x1 = x + k;
      y1 = y;
    }
    vline(pixels, stride, img_w, img_h, x1, y1, y1 + len - 1, color);
  }
}

/* Per-edge color triplets: [0]=dark, [1]=light, [2]=base for each edge. */
struct edge_colors {
  uint32_t n[3], s[3], w[3], e[3];
};

static void
edge_colors_init(struct edge_colors *ec, const struct hikari_border_color *bc)
{
  float lt[4], dk[4];

  hikari_color_lighten(lt, bc->n);
  hikari_color_darken(dk, bc->n);
  ec->n[0] = F2PX(dk);
  ec->n[1] = F2PX(lt);
  ec->n[2] = F2PX(bc->n);

  hikari_color_lighten(lt, bc->s);
  hikari_color_darken(dk, bc->s);
  ec->s[0] = F2PX(dk);
  ec->s[1] = F2PX(lt);
  ec->s[2] = F2PX(bc->s);

  hikari_color_lighten(lt, bc->w);
  hikari_color_darken(dk, bc->w);
  ec->w[0] = F2PX(dk);
  ec->w[1] = F2PX(lt);
  ec->w[2] = F2PX(bc->w);

  hikari_color_lighten(lt, bc->e);
  hikari_color_darken(dk, bc->e);
  ec->e[0] = F2PX(dk);
  ec->e[1] = F2PX(lt);
  ec->e[2] = F2PX(bc->e);
}

/* FVWM 7-layer relief tables. */
static const int fvwm_ul[] = { 0, 1, 1, -1, 3, 0, 0 };
static const int fvwm_br[] = { 0, 0, 3, -1, 1, 1, 0 };

/*
 * Compute layer widths for FVWM or MWM style.
 * Center gap (w[3]) absorbs the remainder.
 */
static void
compute_layer_widths(int w[7], int bws, int style)
{
  if (style == HIKARI_BORDER_STYLE_MWM) {
    w[0] = 0;
    w[1] = 2;
    w[2] = 0;
    w[3] = 0;
    w[4] = 0;
    w[5] = 1;
    w[6] = 0;
    int sum = 3;
    if (bws < sum) {
      int trim = sum - bws;
      if (trim > 0 && w[1] > 1) {
        w[1]--;
        trim--;
        sum--;
      }
      if (trim > 0 && w[5] > 0) {
        w[5] = 0;
        trim--;
        sum--;
      }
      if (trim > 0 && w[1] > 0) {
        w[1] = 0;
        trim--;
        sum--;
      }
    }
    w[3] = bws - sum;
    if (w[3] < 0)
      w[3] = 0;
  } else {
    w[0] = 1;
    w[1] = 1;
    w[2] = 1;
    w[3] = 0;
    w[4] = 1;
    w[5] = 1;
    w[6] = 1;
    int sum = 6;
    if (bws < sum) {
      int trim = sum - bws;
      if (trim > 0 && w[1] > 1) {
        w[1]--;
        trim--;
        sum--;
      }
      if (trim > 0 && w[5] > 0) {
        w[5] = 0;
        trim--;
        sum--;
      }
      if (trim > 0 && w[1] > 0) {
        w[1] = 0;
        trim--;
        sum--;
      }
      if (trim > 0 && w[2] > 0) {
        w[2] = 0;
        trim--;
        sum--;
      }
      if (trim > 0 && w[4] > 0) {
        w[4] = 0;
        trim--;
        sum--;
      }
      if (trim > 0 && w[6] > 0) {
        w[6] = 0;
        trim--;
        sum--;
      }
    }
    w[3] = bws - sum;
    if (w[3] < 0)
      w[3] = 0;
  }
}

/*
 * Fill the base color for the border ring.  Each pixel is assigned to
 * its nearest edge; corners use a diagonal split.  Explicit corner
 * colors override the split.
 */
static void
fill_border_base(uint32_t *pixels,
    int stride,
    int fw_s,
    int fh_s,
    int bws,
    int cls,
    const struct edge_colors *ec,
    const struct hikari_border_color *colors)
{
  for (int y = 0; y < fh_s; y++) {
    for (int x = 0; x < fw_s; x++) {
      int dt = y, db = fh_s - 1 - y, dl = x, dr = fw_s - 1 - x;
      int dmin = dt;
      if (db < dmin)
        dmin = db;
      if (dl < dmin)
        dmin = dl;
      if (dr < dmin)
        dmin = dr;
      if (dmin >= bws)
        continue;

      bool in_top = (dt < cls), in_bot = (db < cls);
      bool in_left = (dl < cls), in_right = (dr < cls);
      uint32_t px;

      if (in_top && in_left) {
        px = colors->has_nw ? F2PX(colors->nw)
                            : ((dt <= dl) ? ec->n[2] : ec->w[2]);
      } else if (in_top && in_right) {
        px = colors->has_ne ? F2PX(colors->ne)
                            : ((dt <= dr) ? ec->n[2] : ec->e[2]);
      } else if (in_bot && in_left) {
        px = colors->has_sw ? F2PX(colors->sw)
                            : ((db <= dl) ? ec->s[2] : ec->w[2]);
      } else if (in_bot && in_right) {
        px = colors->has_se ? F2PX(colors->se)
                            : ((db <= dr) ? ec->s[2] : ec->e[2]);
      } else if (dt == dmin) {
        px = ec->n[2];
      } else if (db == dmin) {
        px = ec->s[2];
      } else if (dl == dmin) {
        px = ec->w[2];
      } else {
        px = ec->e[2];
      }
      pixels[y * stride + x] = px;
    }
  }
}

/*
 * Draw concentric relief rectangles over the full frame using per-edge
 * colors.  Each layer draws left+top (UL) then bottom+right (BR).
 */
static void
draw_relief_rings(uint32_t *pixels,
    int stride,
    int fw_s,
    int fh_s,
    const int w[7],
    const struct edge_colors *ec)
{
  int off = 0, rw = fw_s - 1, rh = fh_s - 1;

  for (int i = 0; i < 7; i++) {
    if (w[i] <= 0)
      continue;
    if (fvwm_ul[i] < 0) {
      off += w[i];
      rw -= 2 * w[i];
      rh -= 2 * w[i];
      continue;
    }
    if (rw <= 0 || rh <= 0)
      break;

    uint32_t ul_w = (fvwm_ul[i] == 3) ? TRANSPARENT_PX : ec->w[fvwm_ul[i]];
    uint32_t ul_n = (fvwm_ul[i] == 3) ? TRANSPARENT_PX : ec->n[fvwm_ul[i]];
    uint32_t br_s = (fvwm_br[i] == 3) ? TRANSPARENT_PX : ec->s[fvwm_br[i]];
    uint32_t br_e = (fvwm_br[i] == 3) ? TRANSPARENT_PX : ec->e[fvwm_br[i]];

    for (int k = 0; k < w[i]; k++) {
      relieve_rect_per_edge(pixels,
          stride,
          fw_s,
          fh_s,
          off,
          off,
          rw,
          rh,
          1,
          ul_w,
          ul_n,
          br_s,
          br_e);
      off++;
      rw -= 2;
      rh -= 2;
    }
  }
}

/*
 * Overdraw relief within explicit corner regions using corner-derived
 * colors.
 */
static void
overdraw_explicit_corners(uint32_t *pixels,
    int stride,
    int fw_s,
    int fh_s,
    int cls,
    const int w[7],
    const struct hikari_border_color *colors)
{
  struct {
    bool has;
    const float *color;
    int cx, cy, cw, ch;
    bool ul_vert, ul_horiz, br_horiz, br_vert;
  } corners[4] = {
    { colors->has_nw, colors->nw, 0, 0, cls, cls, true, true, false, false },
    { colors->has_ne,
        colors->ne,
        fw_s - cls,
        0,
        cls,
        cls,
        false,
        true,
        false,
        true },
    { colors->has_sw,
        colors->sw,
        0,
        fh_s - cls,
        cls,
        cls,
        true,
        false,
        true,
        false },
    { colors->has_se,
        colors->se,
        fw_s - cls,
        fh_s - cls,
        cls,
        cls,
        false,
        false,
        true,
        true },
  };

  for (int ci = 0; ci < 4; ci++) {
    if (!corners[ci].has)
      continue;

    float clt[4], cdk[4];
    hikari_color_lighten(clt, corners[ci].color);
    hikari_color_darken(cdk, corners[ci].color);
    uint32_t cc[3] = { F2PX(cdk), F2PX(clt), F2PX(corners[ci].color) };

    int ccx = corners[ci].cx, ccy = corners[ci].cy;
    int ccw = corners[ci].cw, cch = corners[ci].ch;

    int off = 0, rw = fw_s - 1, rh = fh_s - 1;

    for (int i = 0; i < 7; i++) {
      if (w[i] <= 0)
        continue;
      if (fvwm_ul[i] < 0) {
        off += w[i];
        rw -= 2 * w[i];
        rh -= 2 * w[i];
        continue;
      }
      if (rw <= 0 || rh <= 0)
        break;

      uint32_t ul = (fvwm_ul[i] == 3) ? TRANSPARENT_PX : cc[fvwm_ul[i]];
      uint32_t br = (fvwm_br[i] == 3) ? TRANSPARENT_PX : cc[fvwm_br[i]];

      for (int k = 0; k < w[i]; k++) {
        int rx = off + k, ry = off + k;
        int rrw = rw - 2 * k, rrh = rh - 2 * k;

        if (corners[ci].ul_vert && ul != TRANSPARENT_PX && rx >= ccx &&
            rx < ccx + ccw) {
          int y1 = ry, y2 = ry + rrh - 1;
          if (y1 < ccy)
            y1 = ccy;
          if (y2 >= ccy + cch)
            y2 = ccy + cch - 1;
          if (y1 <= y2)
            vline(pixels, stride, fw_s, fh_s, rx, y1, y2, ul);
        }
        if (corners[ci].ul_horiz && ul != TRANSPARENT_PX && ry >= ccy &&
            ry < ccy + cch) {
          int x1 = rx + 1, x2 = rx + rrw;
          if (x1 < ccx)
            x1 = ccx;
          if (x2 >= ccx + ccw)
            x2 = ccx + ccw - 1;
          if (x1 <= x2)
            hline(pixels, stride, fw_s, fh_s, x1, x2, ry, ul);
        }
        if (corners[ci].br_horiz && br != TRANSPARENT_PX) {
          int by = ry + rrh;
          if (by >= ccy && by < ccy + cch) {
            int x1 = rx, x2 = rx + rrw - 1;
            if (x1 < ccx)
              x1 = ccx;
            if (x2 >= ccx + ccw)
              x2 = ccx + ccw - 1;
            if (x1 <= x2)
              hline(pixels, stride, fw_s, fh_s, x1, x2, by, br);
          }
        }
        if (corners[ci].br_vert && br != TRANSPARENT_PX) {
          int bx = rx + rrw;
          if (bx >= ccx && bx < ccx + ccw) {
            int y1 = ry + 1, y2 = ry + rrh;
            if (y1 < ccy)
              y1 = ccy;
            if (y2 >= ccy + cch)
              y2 = ccy + cch - 1;
            if (y1 <= y2)
              vline(pixels, stride, fw_s, fh_s, bx, y1, y2, br);
          }
        }
      }
      off += w[i];
      rw -= 2 * w[i];
      rh -= 2 * w[i];
    }
  }
}

/*
 * Draw handle marks on all 8 frame parts (4 sidebars + 4 corners).
 */
static void
draw_handle_marks(uint32_t *pixels,
    int stride,
    int fw_s,
    int fh_s,
    int bws,
    int cls,
    const int w[7],
    int style,
    const struct edge_colors *ec,
    const struct hikari_border_color *colors)
{
  int w_dout = w[0];
  int inset = (w[5] > 0 || w[6] > 0) ? 1 : 0;
  int mark_len = bws - w_dout - inset;
  int mark_thick = (style == HIKARI_BORDER_STYLE_MWM) ? 1 : 2;
  if (mark_thick > mark_len)
    mark_thick = mark_len;
  int offset_tl = w_dout;
  int offset_br = -2 * w_dout - mark_len;

  if (mark_len <= 0 || cls <= bws)
    return;

  int sbw = fw_s - 2 * cls;
  int sbh = fh_s - 2 * cls;

  /* Sidebar marks */
  if (sbw > 0) {
    draw_y_mark(pixels,
        stride,
        fw_s,
        fh_s,
        cls,
        0,
        mark_len,
        mark_thick,
        offset_tl,
        false,
        ec->n[1]);
    draw_y_mark(pixels,
        stride,
        fw_s,
        fh_s,
        cls + sbw,
        0,
        mark_len,
        mark_thick,
        offset_tl,
        true,
        ec->n[0]);

    draw_y_mark(pixels,
        stride,
        fw_s,
        fh_s,
        cls,
        fh_s - bws + bws + offset_br,
        mark_len,
        mark_thick,
        offset_tl,
        false,
        ec->s[1]);
    draw_y_mark(pixels,
        stride,
        fw_s,
        fh_s,
        cls + sbw,
        fh_s - bws + bws + offset_br,
        mark_len,
        mark_thick,
        offset_tl,
        true,
        ec->s[0]);
  }
  if (sbh > 0) {
    draw_x_mark(pixels,
        stride,
        fw_s,
        fh_s,
        0,
        cls,
        mark_len,
        mark_thick,
        offset_tl,
        false,
        ec->w[1]);
    draw_x_mark(pixels,
        stride,
        fw_s,
        fh_s,
        0,
        cls + sbh,
        mark_len,
        mark_thick,
        offset_tl,
        true,
        ec->w[0]);

    draw_x_mark(pixels,
        stride,
        fw_s,
        fh_s,
        fw_s - bws + bws + offset_br,
        cls,
        mark_len,
        mark_thick,
        offset_tl,
        false,
        ec->e[1]);
    draw_x_mark(pixels,
        stride,
        fw_s,
        fh_s,
        fw_s - bws + bws + offset_br,
        cls + sbh,
        mark_len,
        mark_thick,
        offset_tl,
        true,
        ec->e[0]);
  }

  /* Corner marks. */
  struct {
    bool has_explicit;
    const float *color;
    int cx, cy;
    int xm_x, xm_y;
    bool xm_shadow;
    int ym_x, ym_y;
    bool ym_shadow;
    uint32_t def_xm, def_ym; /* default shadow/relief for x_mark, y_mark */
  } cm[4] = {
    { colors->has_nw,
        colors->nw,
        0,
        0,
        0,
        cls,
        true,
        cls,
        0,
        true,
        ec->w[0],
        ec->n[0] },
    { colors->has_ne,
        colors->ne,
        fw_s - cls,
        0,
        cls + offset_br,
        cls,
        true,
        0,
        0,
        false,
        ec->e[0],
        ec->n[1] },
    { colors->has_sw,
        colors->sw,
        0,
        fh_s - cls,
        0,
        0,
        false,
        cls,
        cls + offset_br,
        true,
        ec->w[1],
        ec->s[0] },
    { colors->has_se,
        colors->se,
        fw_s - cls,
        fh_s - cls,
        cls + offset_br,
        0,
        false,
        0,
        cls + offset_br,
        false,
        ec->e[1],
        ec->s[1] },
  };

  for (int ci = 0; ci < 4; ci++) {
    uint32_t xm_color, ym_color;
    if (cm[ci].has_explicit) {
      float clt[4], cdk[4];
      hikari_color_lighten(clt, cm[ci].color);
      hikari_color_darken(cdk, cm[ci].color);
      xm_color = cm[ci].xm_shadow ? F2PX(cdk) : F2PX(clt);
      ym_color = cm[ci].ym_shadow ? F2PX(cdk) : F2PX(clt);
    } else {
      xm_color = cm[ci].def_xm;
      ym_color = cm[ci].def_ym;
    }
    draw_x_mark(pixels,
        stride,
        fw_s,
        fh_s,
        cm[ci].cx + cm[ci].xm_x,
        cm[ci].cy + cm[ci].xm_y,
        mark_len,
        mark_thick,
        offset_tl,
        cm[ci].xm_shadow,
        xm_color);
    draw_y_mark(pixels,
        stride,
        fw_s,
        fh_s,
        cm[ci].cx + cm[ci].ym_x,
        cm[ci].cy + cm[ci].ym_y,
        mark_len,
        mark_thick,
        offset_tl,
        cm[ci].ym_shadow,
        ym_color);
  }
}

bool
hikari_border_frame_generate(struct hikari_border *border,
    struct wlr_renderer *renderer,
    const struct hikari_border_color *colors,
    int border_width,
    int corner_length,
    float scale,
    int style)
{
  hikari_border_frame_fini(border);

  int frame_w = border->geometry.width;
  int frame_h = border->geometry.height;

  if (frame_w <= 0 || frame_h <= 0)
    return false;

  int fw_s = (int)(frame_w * scale);
  int fh_s = (int)(frame_h * scale);
  int bws = (int)(border_width * scale);
  int cls = (int)(corner_length * scale);

  if (bws < 1)
    bws = 1;
  if (cls < bws)
    cls = bws;
  if (fw_s < 2 || fh_s < 2)
    return false;

  pixman_image_t *image =
      pixman_image_create_bits(PIXMAN_a8r8g8b8, fw_s, fh_s, NULL, fw_s * 4);
  if (image == NULL)
    return false;

  /* Clear to fully transparent */
  pixman_color_t clear = { 0, 0, 0, 0 };
  pixman_image_t *cf = pixman_image_create_solid_fill(&clear);
  if (cf) {
    pixman_image_composite32(
        PIXMAN_OP_SRC, cf, NULL, image, 0, 0, 0, 0, 0, 0, fw_s, fh_s);
    pixman_image_unref(cf);
  }

  struct edge_colors ec;
  edge_colors_init(&ec, colors);

  uint32_t *pixels = pixman_image_get_data(image);
  int stride = pixman_image_get_stride(image) / 4;

  /* Fill base colors for the border ring */
  fill_border_base(pixels, stride, fw_s, fh_s, bws, cls, &ec, colors);

  /* Compute layer widths for the selected style */
  int w[7];
  compute_layer_widths(w, bws, style);

  /* Draw concentric relief rectangles */
  draw_relief_rings(pixels, stride, fw_s, fh_s, w, &ec);

  /* Overdraw corners with explicit colors */
  overdraw_explicit_corners(pixels, stride, fw_s, fh_s, cls, w, colors);

  /* Draw handle marks on all 8 frame parts */
  draw_handle_marks(
      pixels, stride, fw_s, fh_s, bws, cls, w, style, &ec, colors);

  uint32_t *data = pixman_image_get_data(image);
  border->frame_texture = wlr_texture_from_pixels(
      renderer, DRM_FORMAT_ARGB8888, fw_s * 4, fw_s, fh_s, data);

  pixman_image_unref(image);

  if (border->frame_texture != NULL) {
    border->frame_w = fw_s;
    border->frame_h = fh_s;
    border->frame_scale = scale;
    border->frame_dirty = false;
    border->frame_generated_state = border->state;
    return true;
  }
  return false;
}
