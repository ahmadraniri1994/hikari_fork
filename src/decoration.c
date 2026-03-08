#include <hikari/decoration.h>

#include <hikari/memory.h>

#include <wlr/types/wlr_xdg_shell.h>

static void
set_mode(struct hikari_decoration *decoration)
{
  wlr_xdg_toplevel_decoration_v1_set_mode(
      decoration->decoration, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static void
request_mode_handler(struct wl_listener *listener, void *data)
{
  struct hikari_decoration *decoration =
      wl_container_of(listener, decoration, request_mode);

  struct wlr_xdg_surface *xdg_surface = decoration->decoration->toplevel->base;

  if (!xdg_surface->initialized) {
    return;
  }

  set_mode(decoration);
}

static void
surface_commit_handler(struct wl_listener *listener, void *data)
{
  struct hikari_decoration *decoration =
      wl_container_of(listener, decoration, surface_configure);

  struct wlr_xdg_surface *xdg_surface = decoration->decoration->toplevel->base;

  /* Wait until the surface is initialized (has acked an initial configure). */
  if (!xdg_surface->initialized) {
    return;
  }

  set_mode(decoration);

  /* Disconnect: we only need to send the mode once. */
  wl_list_remove(&decoration->surface_configure.link);
  wl_list_init(&decoration->surface_configure.link);
}

static void
destroy_handler(struct wl_listener *listener, void *data)
{
  struct hikari_decoration *decoration =
      wl_container_of(listener, decoration, destroy);

  wl_list_remove(&decoration->request_mode.link);
  wl_list_remove(&decoration->destroy.link);
  wl_list_remove(&decoration->surface_configure.link);

  hikari_free(decoration);
}

void
hikari_decoration_init(struct hikari_decoration *decoration,
    struct wlr_xdg_toplevel_decoration_v1 *wlr_decoration)
{
  decoration->decoration = wlr_decoration;

  decoration->request_mode.notify = request_mode_handler;
  wl_signal_add(
      &wlr_decoration->events.request_mode, &decoration->request_mode);

  decoration->destroy.notify = destroy_handler;
  wl_signal_add(&wlr_decoration->events.destroy, &decoration->destroy);

  wl_list_init(&decoration->surface_configure.link);

  struct wlr_xdg_surface *xdg_surface = wlr_decoration->toplevel->base;

  if (xdg_surface->initialized) {
    /* Surface already initialized — safe to set mode immediately. */
    set_mode(decoration);
  } else {
    /* Defer: listen to the underlying wlr_surface commit events until
     * the xdg_surface becomes initialized (after acking its initial
     * configure). Using surface->events.commit avoids re-entering
     * wlr_xdg_surface_schedule_configure. */
    decoration->surface_configure.notify = surface_commit_handler;
    wl_signal_add(
        &xdg_surface->surface->events.commit, &decoration->surface_configure);
  }
}
