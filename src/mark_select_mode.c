#include <hikari/mark_select_mode.h>

#include <assert.h>
#include <stdbool.h>

#include <hikari/cursor.h>
#include <hikari/keyboard.h>
#include <hikari/mark.h>
#include <hikari/renderer.h>
#include <hikari/server.h>
#include <hikari/workspace.h>

#include <hikari/compat.h>

static void
mark_select(struct hikari_workspace *workspace,
    struct wlr_keyboard_key_event *event,
    struct hikari_keyboard *keyboard)
{
  (void)workspace;
  uint32_t keycode = event->keycode + 8;
  uint32_t codepoint = hikari_keyboard_get_codepoint(keyboard, keycode);

  hikari_server_enter_normal_mode(NULL);

  struct hikari_mark *mark;
  if (hikari_mark_get(codepoint, &mark)) {
    assert(mark != NULL);
    if (hikari_server.mark_select_mode.switch_workspace) {
      hikari_server_switch_to_mark(mark);
    } else {
      hikari_server_show_mark(mark);
    }
  }
}

static void
key_handler(
    struct hikari_keyboard *keyboard, struct wlr_keyboard_key_event *event)
{
  struct hikari_workspace *workspace = hikari_server.workspace;

  if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
    mark_select(workspace, event, keyboard);
  }
}

static void
modifiers_handler(__unused struct hikari_keyboard *keyboard)
{
  return;
}

static void
cancel(void)
{
  return;
}

static void
button_handler(
    __unused struct hikari_cursor *cursor, __unused struct wlr_pointer_button_event *event)
{
  return;
}

static void
cursor_move(__unused uint32_t time_msec)
{
  return;
}

void
hikari_mark_select_mode_init(struct hikari_mark_select_mode *mark_select_mode)
{
  mark_select_mode->mode.key_handler = key_handler;
  mark_select_mode->mode.button_handler = button_handler;
  mark_select_mode->mode.modifiers_handler = modifiers_handler;
  mark_select_mode->mode.render = hikari_renderer_mark_select_mode;
  mark_select_mode->mode.cancel = cancel;
  mark_select_mode->mode.cursor_move = cursor_move;

  mark_select_mode->switch_workspace = false;
}

void
hikari_mark_select_mode_enter(bool switch_workspace)
{
  struct hikari_server *server = &hikari_server;

  assert(server->workspace != NULL);

  hikari_workspace_focus_view(server->workspace, NULL);

  server->mark_select_mode.switch_workspace = switch_workspace;
  server->mode = (struct hikari_mode *)&server->mark_select_mode;

  hikari_cursor_set_image(&hikari_server.cursor, "link");
}
