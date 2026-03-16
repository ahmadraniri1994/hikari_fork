#include <hikari/renderer.h>

#include <assert.h>

#include <hikari/color.h>
#include <hikari/geometry.h>
#include <hikari/output.h>
#include <hikari/renderer.h>
#include <hikari/server.h>
#include <hikari/view.h>

#include <hikari/compat.h>

#include <hikari/border_style.h>
#include <hikari/configuration.h>
#include <hikari/server.h>

#ifdef HAVE_XWAYLAND
#include <hikari/xwayland_unmanaged_view.h>
#include <hikari/xwayland_view.h>
#endif

#include <wlr/backend.h>
#include <wlr/render/pass.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/region.h>
#include <wlr/util/transform.h>

#ifdef HAVE_XWAYLAND
#include <wlr/xwayland.h>
#endif

static inline void
rect_render(float color[static 4],
    struct wlr_box *box,
    struct hikari_renderer *renderer)
{
  pixman_region32_t damage;
  pixman_region32_init(&damage);
  pixman_region32_union_rect(
      &damage, &damage, box->x, box->y, box->width, box->height);

  pixman_region32_intersect(&damage, &damage, renderer->damage);
  bool damaged = pixman_region32_not_empty(&damage);
  if (!damaged) {
    goto buffer_damage_finish;
  }

  struct wlr_render_color render_color = {
    .r = color[0], .g = color[1], .b = color[2], .a = color[3]
  };
  struct wlr_render_rect_options opts = {
    .box = *box,
    .color = render_color,
    .clip = &damage,
  };
  wlr_render_pass_add_rect(renderer->render_pass, &opts);

buffer_damage_finish:
  pixman_region32_fini(&damage);
}

/*
 * Blit a region of a wlr_texture to a destination box.
 */
static inline void
frame_blit(struct wlr_texture *texture,
    struct wlr_box *dst_box,
    int src_x,
    int src_y,
    int src_w,
    int src_h,
    pixman_region32_t *clip,
    struct hikari_renderer *renderer)
{
  struct wlr_render_texture_options opts = {
    .texture = texture,
    .src_box = {
      .x = src_x,
      .y = src_y,
      .width = src_w,
      .height = src_h,
    },
    .dst_box = *dst_box,
    .clip = clip,
    .filter_mode = WLR_SCALE_FILTER_NEAREST,
  };
  wlr_render_pass_add_texture(renderer->render_pass, &opts);
}

/*
 * Render a per-view FVWM-style border frame.
 *
 * The frame texture is a full frame-sized image (frame_w x frame_h scaled)
 * with the border ring rendered and the center transparent.  Each of the
 * 8 border parts samples from its corresponding frame-relative position.
 */
static inline void
render_border_fvwm_frame(struct hikari_border *border,
    pixman_region32_t *clip,
    struct hikari_renderer *renderer)
{
  struct wlr_texture *tex = border->frame_texture;
  float scale = border->frame_scale;
  int bx = border->geometry.x;
  int by = border->geometry.y;

  /*
   * Each part's source region in the frame texture corresponds to its
   * position relative to the frame origin (bx, by), scaled.
   */

  /* Top sidebar */
  {
    int sx = (int)((border->top.x - bx) * scale);
    int sy = (int)((border->top.y - by) * scale);
    int sw = (int)(border->top.width * scale);
    int sh = (int)(border->top.height * scale);
    frame_blit(tex, &border->top, sx, sy, sw, sh, clip, renderer);
  }

  /* Bottom sidebar */
  {
    int sx = (int)((border->bottom.x - bx) * scale);
    int sy = (int)((border->bottom.y - by) * scale);
    int sw = (int)(border->bottom.width * scale);
    int sh = (int)(border->bottom.height * scale);
    frame_blit(tex, &border->bottom, sx, sy, sw, sh, clip, renderer);
  }

  /* Left sidebar */
  {
    int sx = (int)((border->left.x - bx) * scale);
    int sy = (int)((border->left.y - by) * scale);
    int sw = (int)(border->left.width * scale);
    int sh = (int)(border->left.height * scale);
    frame_blit(tex, &border->left, sx, sy, sw, sh, clip, renderer);
  }

  /* Right sidebar */
  {
    int sx = (int)((border->right.x - bx) * scale);
    int sy = (int)((border->right.y - by) * scale);
    int sw = (int)(border->right.width * scale);
    int sh = (int)(border->right.height * scale);
    frame_blit(tex, &border->right, sx, sy, sw, sh, clip, renderer);
  }

  /* NW corner */
  {
    int sx = (int)((border->corner_nw.x - bx) * scale);
    int sy = (int)((border->corner_nw.y - by) * scale);
    int sw = (int)(border->corner_nw.width * scale);
    int sh = (int)(border->corner_nw.height * scale);
    frame_blit(tex, &border->corner_nw, sx, sy, sw, sh, clip, renderer);
  }

  /* NE corner */
  {
    int sx = (int)((border->corner_ne.x - bx) * scale);
    int sy = (int)((border->corner_ne.y - by) * scale);
    int sw = (int)(border->corner_ne.width * scale);
    int sh = (int)(border->corner_ne.height * scale);
    frame_blit(tex, &border->corner_ne, sx, sy, sw, sh, clip, renderer);
  }

  /* SW corner */
  {
    int sx = (int)((border->corner_sw.x - bx) * scale);
    int sy = (int)((border->corner_sw.y - by) * scale);
    int sw = (int)(border->corner_sw.width * scale);
    int sh = (int)(border->corner_sw.height * scale);
    frame_blit(tex, &border->corner_sw, sx, sy, sw, sh, clip, renderer);
  }

  /* SE corner */
  {
    int sx = (int)((border->corner_se.x - bx) * scale);
    int sy = (int)((border->corner_se.y - by) * scale);
    int sw = (int)(border->corner_se.width * scale);
    int sh = (int)(border->corner_se.height * scale);
    frame_blit(tex, &border->corner_se, sx, sy, sw, sh, clip, renderer);
  }
}

static inline void
render_border(struct hikari_border *border, struct hikari_renderer *renderer)
{
  if (border->state == HIKARI_BORDER_NONE) {
    return;
  }

  struct wlr_box *geometry = &border->geometry;

  pixman_region32_t damage;
  pixman_region32_init(&damage);
  pixman_region32_union_rect(&damage,
      &damage,
      geometry->x,
      geometry->y,
      geometry->width,
      geometry->height);
  pixman_region32_intersect(&damage, &damage, renderer->damage);

  bool damaged = pixman_region32_not_empty(&damage);
  if (!damaged) {
    goto buffer_damage_finish;
  }

  if (hikari_configuration->border_style != HIKARI_BORDER_STYLE_NONE) {
    /*
     * Per-view frame rendering path (both FVWM and MWM styles).
     *
     * Lazily (re)generate the frame texture if the texture is missing,
     * the output scale changed, or the border state changed.
     */
    struct hikari_output *output = renderer->wlr_output->data;
    float scale = output->wlr_output->scale;
    int border_style = hikari_configuration->border_style;

    if (border->frame_texture == NULL || border->frame_scale != scale ||
        border->frame_generated_state != border->state) {
      struct hikari_border_color *colors;
      switch (border->state) {
        case HIKARI_BORDER_INACTIVE:
          colors = &hikari_configuration->border_inactive;
          break;
        case HIKARI_BORDER_ACTIVE:
          colors = &hikari_configuration->border_active;
          break;
        default:
          goto buffer_damage_finish;
      }
      int bw = hikari_configuration->border;
      int cl = border->corner_nw.width;
      if (cl < bw)
        cl = bw;
      hikari_border_frame_generate(
          border, hikari_server.renderer, colors, bw, cl, scale, border_style);
    }

    if (border->frame_texture != NULL) {
      /* During interactive resize the texture dimensions may not
       * match the current frame -- fall through to flat rendering. */
      int exp_w = (int)(border->geometry.width * scale);
      int exp_h = (int)(border->geometry.height * scale);
      if (border->frame_w == exp_w && border->frame_h == exp_h) {
        render_border_fvwm_frame(border, &damage, renderer);
        goto flat_corners;
      }
    }
  }

  {
    struct hikari_border_color *colors;
    switch (border->state) {
      case HIKARI_BORDER_INACTIVE:
        colors = &hikari_configuration->border_inactive;
        break;

      case HIKARI_BORDER_ACTIVE:
        colors = &hikari_configuration->border_active;
        break;

      default:
        goto buffer_damage_finish;
    }

    rect_render(colors->n, &border->top, renderer);
    rect_render(colors->s, &border->bottom, renderer);
    rect_render(colors->w, &border->left, renderer);
    rect_render(colors->e, &border->right, renderer);

    rect_render(colors->nw, &border->corner_nw, renderer);
    rect_render(colors->ne, &border->corner_ne, renderer);
    rect_render(colors->sw, &border->corner_sw, renderer);
    rect_render(colors->se, &border->corner_se, renderer);

    goto buffer_damage_finish;
  }

flat_corners:
  /* Border style path already rendered corners via texture */

buffer_damage_finish:
  pixman_region32_fini(&damage);
}

static void
render_indicator_bar(struct hikari_indicator_bar *indicator_bar,
    struct hikari_renderer *renderer)
{
  if (indicator_bar->texture == NULL) {
    return;
  }

  struct wlr_box *geometry = renderer->geometry;

  geometry->width = indicator_bar->width;
  geometry->height = hikari_configuration->font.height;

  pixman_region32_t clip;
  pixman_region32_init_rect(
      &clip, geometry->x, geometry->y, geometry->width, geometry->height);

  struct wlr_render_texture_options opts = {
    .texture = indicator_bar->texture,
    .dst_box = *geometry,
    .clip = &clip,
  };
  wlr_render_pass_add_texture(renderer->render_pass, &opts);

  pixman_region32_fini(&clip);
}

static inline void
render_indicator(
    struct hikari_indicator *indicator, struct hikari_renderer *renderer)
{
  struct wlr_box *border_geometry = renderer->geometry;
  struct wlr_box geometry = *border_geometry;

  renderer->geometry = &geometry;

  geometry.x += 5;

  struct hikari_indicator_bar *title_bar = &indicator->title;
  geometry.y += 5;
  render_indicator_bar(title_bar, renderer);

  int bar_height = hikari_configuration->font.height;

  struct hikari_indicator_bar *sheet_bar = &indicator->sheet;
  geometry.y += bar_height + 5;
  render_indicator_bar(sheet_bar, renderer);

  struct hikari_indicator_bar *group_bar = &indicator->group;
  geometry.y += bar_height + 5;
  render_indicator_bar(group_bar, renderer);

  struct hikari_indicator_bar *mark_bar = &indicator->mark;
  geometry.y += bar_height + 5;
  render_indicator_bar(mark_bar, renderer);

  renderer->geometry = border_geometry;
}

static inline void
render_indicator_frame(struct hikari_indicator_frame *indicator_frame,
    float color[static 4],
    struct hikari_renderer *renderer)
{
  struct wlr_box *box = renderer->geometry;

  pixman_region32_t damage;
  pixman_region32_init(&damage);
  pixman_region32_union_rect(
      &damage, &damage, box->x, box->y, box->width, box->height);

  pixman_region32_intersect(&damage, &damage, renderer->damage);
  bool damaged = pixman_region32_not_empty(&damage);
  if (!damaged) {
    goto buffer_damage_finish;
  }

  rect_render(color, &indicator_frame->top, renderer);
  rect_render(color, &indicator_frame->bottom, renderer);
  rect_render(color, &indicator_frame->left, renderer);
  rect_render(color, &indicator_frame->right, renderer);

buffer_damage_finish:
  pixman_region32_fini(&damage);
}

static inline void
clear_output(struct hikari_renderer *renderer)
{
  float *clear_color = hikari_configuration->clear;
  struct wlr_output *wlr_output = renderer->wlr_output;
  pixman_region32_t *damage = renderer->damage;

  int nrects;
  pixman_box32_t *rects = pixman_region32_rectangles(damage, &nrects);
  for (int i = 0; i < nrects; ++i) {
    pixman_box32_t *r = &rects[i];
    struct wlr_box box = {
      .x = r->x1, .y = r->y1, .width = r->x2 - r->x1, .height = r->y2 - r->y1
    };
    (void)wlr_output;

    pixman_region32_t clip;
    pixman_region32_init_rect(&clip, box.x, box.y, box.width, box.height);

    struct wlr_render_color render_color = { .r = clear_color[0],
      .g = clear_color[1],
      .b = clear_color[2],
      .a = clear_color[3] };
    struct wlr_render_rect_options opts = {
      .box = box,
      .color = render_color,
      .clip = &clip,
    };
    wlr_render_pass_add_rect(renderer->render_pass, &opts);
    pixman_region32_fini(&clip);
  }
}

static inline void
render_texture(struct wlr_texture *texture,
    pixman_region32_t *damage,
    struct wlr_render_pass *render_pass,
    struct wlr_box *box,
    float alpha,
    enum wl_output_transform transform)
{
  pixman_region32_t local_damage;
  pixman_region32_init(&local_damage);
  pixman_region32_union_rect(
      &local_damage, &local_damage, box->x, box->y, box->width, box->height);

  pixman_region32_intersect(&local_damage, &local_damage, damage);

  bool damaged = pixman_region32_not_empty(&local_damage);
  if (!damaged) {
    goto damage_finish;
  }

  struct wlr_render_texture_options opts = {
    .texture = texture,
    .dst_box = *box,
    .alpha = &alpha,
    .clip = &local_damage,
    .transform = transform,
  };
  wlr_render_pass_add_texture(render_pass, &opts);

damage_finish:
  pixman_region32_fini(&local_damage);
}

static void
render_surface(struct wlr_surface *surface, int sx, int sy, void *data)
{
  assert(surface != NULL);

  struct wlr_texture *texture = wlr_surface_get_texture(surface);

  if (texture == NULL) {
    return;
  }

  struct hikari_renderer *renderer = data;
  struct wlr_box *geometry = renderer->geometry;
  struct wlr_output *wlr_output = renderer->wlr_output;

  double ox = geometry->x + sx;
  double oy = geometry->y + sy;

  struct wlr_box box = { .x = ox * wlr_output->scale,
    .y = oy * wlr_output->scale,
    .width = surface->current.width * wlr_output->scale,
    .height = surface->current.height * wlr_output->scale };

  enum wl_output_transform transform =
      wlr_output_transform_invert(surface->current.transform);

  render_texture(
      texture, renderer->damage, renderer->render_pass, &box, 1, transform);
}

static inline void
render_background(struct hikari_renderer *renderer, float alpha)
{
  struct hikari_output *output = renderer->wlr_output->data;

  if (output->background == NULL) {
    return;
  }

  struct wlr_output *wlr_output = output->wlr_output;

  struct wlr_box geometry = { .x = 0, .y = 0 };
  wlr_output_transformed_resolution(
      wlr_output, &geometry.width, &geometry.height);

  render_texture(output->background,
      renderer->damage,
      renderer->render_pass,
      &geometry,
      alpha,
      WL_OUTPUT_TRANSFORM_NORMAL);
}

#ifdef HAVE_LAYERSHELL
static inline void
render_layer(struct wl_list *layers, struct hikari_renderer *renderer)
{
  struct hikari_layer *layer;
  wl_list_for_each (layer, layers, layer_surfaces) {
    renderer->geometry = &layer->geometry;
    wlr_layer_surface_v1_for_each_surface(
        layer->surface, render_surface, renderer);
  }
}
#endif

static inline void
render_view(struct hikari_renderer *renderer, struct hikari_view *view)
{
  renderer->geometry = hikari_view_border_geometry(view);

  if (hikari_view_wants_border(view)) {
    render_border(&view->border, renderer);
  }

  struct wlr_box surface_geometry = *hikari_view_geometry(view);
  surface_geometry.x += view->surface_offset_x;
  surface_geometry.y += view->surface_offset_y;
  renderer->geometry = &surface_geometry;

  hikari_node_for_each_surface(
      (struct hikari_node *)view, render_surface, renderer);
}

#ifdef HAVE_XWAYLAND
static inline void
render_unmanaged_views(struct hikari_renderer *renderer)
{
  struct hikari_output *output = renderer->wlr_output->data;

  struct hikari_xwayland_unmanaged_view *xwayland_unmanaged_view;
  wl_list_for_each_reverse (xwayland_unmanaged_view,
      &output->unmanaged_xwayland_views,
      unmanaged_output_views) {

    renderer->geometry = &xwayland_unmanaged_view->geometry;

    wlr_surface_for_each_surface(
        xwayland_unmanaged_view->surface->surface, render_surface, renderer);
  }
}
#endif

static inline void
render_workspace(struct hikari_renderer *renderer)
{
  struct hikari_output *output = renderer->wlr_output->data;

#ifdef HAVE_LAYERSHELL
  render_layer(&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND], renderer);
  render_layer(&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM], renderer);
#endif

  struct hikari_view *view;
  wl_list_for_each_reverse (view, &output->workspace->views, workspace_views) {
    render_view(renderer, view);
  }

#ifdef HAVE_LAYERSHELL
  render_layer(&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP], renderer);
#endif

#ifdef HAVE_XWAYLAND
  render_unmanaged_views(renderer);
#endif
}

#ifdef HAVE_LAYERSHELL
static inline void
render_overlay(struct hikari_renderer *renderer)
{
  struct hikari_output *output = renderer->wlr_output->data;
  render_layer(&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY], renderer);
}
#endif

static inline void
render_output(struct hikari_output *output, pixman_region32_t *damage)
{
  struct wlr_output *wlr_output = output->wlr_output;

  struct wlr_output_state state;
  wlr_output_state_init(&state);

  struct wlr_render_pass *render_pass =
      wlr_output_begin_render_pass(wlr_output, &state, NULL);
  if (render_pass == NULL) {
    wlr_output_state_finish(&state);
    wlr_damage_ring_add(&output->damage, damage);
    wlr_output_schedule_frame(wlr_output);
    return;
  }

  /* Extend damage with what differs in older buffers (multi-buffering). */
  wlr_damage_ring_rotate_buffer(&output->damage, state.buffer, damage);

  struct hikari_renderer renderer = {
    .wlr_output = wlr_output,
    .render_pass = render_pass,
    .damage = damage,
  };

  clear_output(&renderer);
  hikari_server.mode->render(&renderer);

  wlr_output_add_software_cursors_to_render_pass(
      wlr_output, render_pass, damage);
  wlr_render_pass_submit(render_pass);
  wlr_output_commit_state(wlr_output, &state);
  wlr_output_state_finish(&state);
}

#ifdef HAVE_LAYERSHELL
static inline void
layer_for_each(struct wl_list *layers,
    void (*func)(struct wlr_surface *, int, int, void *),
    void *data)
{
  struct hikari_layer *layer;
  wl_list_for_each (layer, layers, layer_surfaces) {
    wlr_layer_surface_v1_for_each_surface(layer->surface, func, data);
  }
}
#endif

static void
send_frame_done(
    struct wlr_surface *surface, __unused int sx, __unused int sy, void *data)
{
  assert(surface != NULL);

  struct timespec *now = data;
  wlr_surface_send_frame_done(surface, now);
}

static inline void
frame_done(struct hikari_output *output)
{
  struct hikari_view *view;
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  wl_list_for_each_reverse (view, &output->views, output_views) {
    hikari_node_for_each_surface(
        (struct hikari_node *)view, send_frame_done, &now);
  }

#ifdef HAVE_XWAYLAND
  struct hikari_xwayland_unmanaged_view *xwayland_unmanaged_view;
  wl_list_for_each_reverse (xwayland_unmanaged_view,
      &output->unmanaged_xwayland_views,
      unmanaged_output_views) {
    wlr_surface_for_each_surface(
        xwayland_unmanaged_view->surface->surface, send_frame_done, &now);
  }
#endif

#ifdef HAVE_LAYERSHELL
  for (int i = 0; i < 4; i++) {
    layer_for_each(&output->layers[i], send_frame_done, &now);
  }
#endif
}

void
hikari_renderer_damage_frame_handler(
    struct wl_listener *listener, __unused void *data)
{
  struct hikari_output *output =
      wl_container_of(listener, output, damage_frame);

  struct wlr_output *wlr_output = output->wlr_output;
  bool has_damage = pixman_region32_not_empty(&output->damage.current);

  if (!wlr_output->needs_frame && !has_damage) {
    frame_done(output);
    return;
  }

  pixman_region32_t buffer_damage;
  pixman_region32_init(&buffer_damage);

  /* Copy current damage — do NOT clear it here; wlr_damage_ring_rotate_buffer
   * reads ring->current and clears it internally. */
  pixman_region32_copy(&buffer_damage, &output->damage.current);

  if (!pixman_region32_not_empty(&buffer_damage)) {
    /* needs_frame with no damage — e.g. cursor update or first frame.
     * Force a full repaint. */
    int out_width, out_height;
    wlr_output_transformed_resolution(wlr_output, &out_width, &out_height);
    pixman_region32_union_rect(
        &buffer_damage, &buffer_damage, 0, 0, out_width, out_height);
  }

  render_output(output, &buffer_damage);
  pixman_region32_fini(&buffer_damage);
  frame_done(output);
}

static inline void
render_public_views(struct hikari_renderer *renderer)
{
  struct hikari_output *output = renderer->wlr_output->data;

  struct hikari_view *view;
  wl_list_for_each_reverse (view, &output->views, output_views) {
    if (hikari_view_is_public(view) && !hikari_view_is_hidden(view)) {
      renderer->geometry = hikari_view_border_geometry(view);

      if (hikari_view_wants_border(view)) {
        render_border(&view->border, renderer);
      }

      renderer->geometry = hikari_view_geometry(view);

      hikari_node_for_each_surface(
          (struct hikari_node *)view, render_surface, renderer);
    }
  }
}

static inline void
render_normal_mode_indication(
    struct hikari_renderer *renderer, struct hikari_view *focus_view)
{
  struct hikari_output *output = renderer->wlr_output->data;
  struct hikari_group *group = focus_view->group;
  struct hikari_view *first = hikari_group_first_view(group);
  float *indicator_first = hikari_configuration->indicator_first;
  float *indicator_grouped = hikari_configuration->indicator_grouped;

  struct hikari_view *view;
  wl_list_for_each_reverse (view, &group->visible_views, visible_group_views) {
    if (view != focus_view && view->output == output) {
      renderer->geometry = hikari_view_border_geometry(view);

      if (first == view) {
        render_indicator_frame(
            &view->indicator_frame, indicator_first, renderer);
      } else {
        render_indicator_frame(
            &view->indicator_frame, indicator_grouped, renderer);
      }
    }
  }

  if (focus_view->output == output) {
    renderer->geometry = hikari_view_border_geometry(focus_view);

    render_indicator_frame(&focus_view->indicator_frame,
        hikari_configuration->indicator_selected,
        renderer);

    render_indicator(&hikari_server.indicator, renderer);
  }
}

static inline void
render_cycling_workspace(
    struct hikari_renderer *renderer, struct hikari_view *focus_view)
{
  struct hikari_output *output = renderer->wlr_output->data;

#ifdef HAVE_LAYERSHELL
  render_layer(&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND], renderer);
  render_layer(&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM], renderer);
#endif

  struct hikari_view *view;
  wl_list_for_each_reverse (view, &output->workspace->views, workspace_views) {
    if (view != focus_view) {
      render_view(renderer, view);
    }
  }

  if (focus_view->output == output) {
    render_view(renderer, focus_view);
  }

#ifdef HAVE_LAYERSHELL
  render_layer(&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP], renderer);
#endif

#ifdef HAVE_XWAYLAND
  render_unmanaged_views(renderer);
#endif
}

void
hikari_renderer_normal_mode(struct hikari_renderer *renderer)
{
  render_background(renderer, 1);

  if (!hikari_server_is_indicating()) {
    render_workspace(renderer);
  } else {
    struct hikari_view *focus_view = hikari_server.workspace->focus_view;

    if (focus_view != NULL) {
      if (hikari_server_is_cycling()) {
        render_cycling_workspace(renderer, focus_view);
      } else {
        render_workspace(renderer);
      }
      render_normal_mode_indication(renderer, focus_view);
    } else {
      render_workspace(renderer);
    }
  }

#ifdef HAVE_LAYERSHELL
  render_overlay(renderer);
#endif
}

void
hikari_renderer_group_assign_mode(struct hikari_renderer *renderer)
{
  struct hikari_output *output = renderer->wlr_output->data;

  render_background(renderer, 1);
  render_workspace(renderer);

  struct hikari_group_assign_mode *mode = &hikari_server.group_assign_mode;

  assert(mode == (struct hikari_group_assign_mode *)hikari_server.mode);

  struct hikari_group *group = mode->group;
  struct hikari_view *focus_view = hikari_server.workspace->focus_view;

  assert(focus_view != NULL);

  if (group != NULL) {
    struct hikari_view *first = hikari_group_first_view(group);
    float *indicator_first = hikari_configuration->indicator_first;
    float *indicator_grouped = hikari_configuration->indicator_grouped;

    struct hikari_view *view;
    wl_list_for_each_reverse (
        view, &group->visible_views, visible_group_views) {
      if (view->output == output && view != focus_view) {
        renderer->geometry = hikari_view_border_geometry(view);

        if (first == view) {
          render_indicator_frame(
              &view->indicator_frame, indicator_first, renderer);
        } else {
          render_indicator_frame(
              &view->indicator_frame, indicator_grouped, renderer);
        }
      }
    }
  }

  if (focus_view->output == output) {
    renderer->geometry = hikari_view_border_geometry(focus_view);

    render_indicator_frame(&focus_view->indicator_frame,
        hikari_configuration->indicator_selected,
        renderer);

    render_indicator(&hikari_server.indicator, renderer);
  }

#ifdef HAVE_LAYERSHELL
  render_overlay(renderer);
#endif
}

void
hikari_renderer_input_grab_mode(struct hikari_renderer *renderer)
{
  struct hikari_output *output = renderer->wlr_output->data;

  render_background(renderer, 1);
  render_workspace(renderer);

  assert(hikari_server.workspace->focus_view != NULL);

  struct hikari_view *view = hikari_server.workspace->focus_view;

  if (view->output == output) {
    renderer->geometry = hikari_view_border_geometry(view);
    render_indicator_frame(&view->indicator_frame,
        hikari_configuration->indicator_insert,
        renderer);
  }

#ifdef HAVE_LAYERSHELL
  render_overlay(renderer);
#endif
}

static inline void
get_lock_indicator_geometry(
    struct hikari_output *output, struct wlr_box *geometry)
{
  const int size = 100;

  geometry->width = size;
  geometry->height = size;

  struct wlr_box output_geometry = { .x = 0,
    .y = 0,
    .width = output->geometry.width,
    .height = output->geometry.height };

  hikari_geometry_position_center(
      geometry, &output_geometry, &geometry->x, &geometry->y);
}

static inline void
render_lock_indicator(struct hikari_renderer *renderer,
    struct hikari_lock_indicator *lock_indicator)
{
  assert(lock_indicator != NULL);

  struct wlr_texture *texture = lock_indicator->current;

  if (texture == NULL) {
    return;
  }

  struct wlr_output *wlr_output = renderer->wlr_output;

  struct wlr_box geometry;
  get_lock_indicator_geometry(wlr_output->data, &geometry);

  pixman_region32_t clip;
  pixman_region32_init_rect(
      &clip, geometry.x, geometry.y, geometry.width, geometry.height);

  struct wlr_render_texture_options opts = {
    .texture = texture,
    .dst_box = geometry,
    .clip = &clip,
  };
  wlr_render_pass_add_texture(renderer->render_pass, &opts);

  pixman_region32_fini(&clip);
}

void
hikari_renderer_lock_mode(struct hikari_renderer *renderer)
{
  struct hikari_lock_mode *mode = &hikari_server.lock_mode;

  assert(mode == (struct hikari_lock_mode *)hikari_server.mode);

  render_background(renderer, 0.1);
  render_public_views(renderer);
  render_lock_indicator(renderer, mode->lock_indicator);
}

void
hikari_renderer_mark_assign_mode(struct hikari_renderer *renderer)
{
  struct hikari_output *output = renderer->wlr_output->data;

  render_background(renderer, 1);
  render_workspace(renderer);

  struct hikari_mark_assign_mode *mode = &hikari_server.mark_assign_mode;

  assert(mode == (struct hikari_mark_assign_mode *)hikari_server.mode);
  assert(hikari_server.workspace->focus_view != NULL);

  struct hikari_view *view = hikari_server.workspace->focus_view;

  if (mode->pending_mark != NULL && mode->pending_mark->view != NULL &&
      mode->pending_mark->view->output == output) {
    renderer->geometry = hikari_view_border_geometry(mode->pending_mark->view);

    render_indicator_frame(&mode->pending_mark->view->indicator_frame,
        hikari_configuration->indicator_conflict,
        renderer);
    render_indicator(&mode->indicator, renderer);
  }

  if (view->output == output) {
    renderer->geometry = hikari_view_border_geometry(view);

    render_indicator_frame(&view->indicator_frame,
        hikari_configuration->indicator_selected,
        renderer);

    render_indicator(&hikari_server.indicator, renderer);
  }

#ifdef HAVE_LAYERSHELL
  render_overlay(renderer);
#endif
}

void
hikari_renderer_move_mode(struct hikari_renderer *renderer)
{
  struct hikari_output *output = renderer->wlr_output->data;

  render_background(renderer, 1);
  render_workspace(renderer);

  struct hikari_view *focus_view = hikari_server.workspace->focus_view;

  if (focus_view->output == output && !hikari_view_is_hidden(focus_view)) {
    renderer->geometry = hikari_view_border_geometry(focus_view);

    render_indicator_frame(&focus_view->indicator_frame,
        hikari_configuration->indicator_insert,
        renderer);

    render_indicator(&hikari_server.indicator, renderer);
  }

#ifdef HAVE_LAYERSHELL
  render_overlay(renderer);
#endif
}

void
hikari_renderer_resize_mode(struct hikari_renderer *renderer)
{
  struct hikari_output *output = renderer->wlr_output->data;

  render_background(renderer, 1);
  render_workspace(renderer);

  struct hikari_view *focus_view = hikari_server.workspace->focus_view;

  if (focus_view->output == output) {
    renderer->geometry = hikari_view_border_geometry(focus_view);

    render_indicator_frame(&focus_view->indicator_frame,
        hikari_configuration->indicator_insert,
        renderer);

    render_indicator(&hikari_server.indicator, renderer);
  }

#ifdef HAVE_LAYERSHELL
  render_overlay(renderer);
#endif
}

void
hikari_renderer_sheet_assign_mode(struct hikari_renderer *renderer)
{
  struct hikari_output *output = renderer->wlr_output->data;

  render_background(renderer, 1);
  render_workspace(renderer);

  assert(hikari_server.workspace->focus_view != NULL);
  struct hikari_view *view = hikari_server.workspace->focus_view;

  if (view->output == output) {
    renderer->geometry = hikari_view_border_geometry(view);

    render_indicator_frame(&view->indicator_frame,
        hikari_configuration->indicator_selected,
        renderer);

    render_indicator(&hikari_server.indicator, renderer);
  }

#ifdef HAVE_LAYERSHELL
  render_overlay(renderer);
#endif
}

static inline void
render_default_workspace(struct hikari_renderer *renderer)
{
  render_background(renderer, 1);
  render_workspace(renderer);
#ifdef HAVE_LAYERSHELL
  render_overlay(renderer);
#endif
}

void
hikari_renderer_dnd_mode(struct hikari_renderer *renderer)
{
  render_default_workspace(renderer);
}

void
hikari_renderer_layout_select_mode(struct hikari_renderer *renderer)
{
  struct hikari_output *output = renderer->wlr_output->data;

  render_background(renderer, 1);
  render_workspace(renderer);

  struct hikari_view *focus_view = hikari_server.workspace->focus_view;

  if (focus_view != NULL && focus_view->output == output) {
    renderer->geometry = hikari_view_border_geometry(focus_view);

    render_indicator_frame(&focus_view->indicator_frame,
        hikari_configuration->indicator_selected,
        renderer);

    render_indicator(&hikari_server.indicator, renderer);
  }

#ifdef HAVE_LAYERSHELL
  render_overlay(renderer);
#endif
}

void
hikari_renderer_mark_select_mode(struct hikari_renderer *renderer)
{
  render_default_workspace(renderer);
}
