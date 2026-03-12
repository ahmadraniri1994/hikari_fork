project(
    'hikari',
    'c',
    license: 'GPL-3.0-or-later',
    default_options: ['c_std=c99', 'warning_level=2', 'werror=false'],
    version: run_command('./tools/hikari-version-str.sh', check: true).stdout().strip(),
)

srcs = [
    'main.c',
    'src/action.c',
    'src/action_config.c',
    'src/binding_config.c',
    'src/binding_group.c',
    'src/border.c',
    'src/command.c',
    'src/completion.c',
    'src/configuration.c',
    'src/cursor.c',
    'src/decoration.c',
    'src/dnd_mode.c',
    'src/exec.c',
    'src/font.c',
    'src/geometry.c',
    'src/group_assign_mode.c',
    'src/group.c',
    'src/indicator_bar.c',
    'src/indicator.c',
    'src/indicator_frame.c',
    'src/input_buffer.c',
    'src/input_grab_mode.c',
    'src/keyboard.c',
    'src/keyboard_config.c',
    'src/layer_shell.c',
    'src/layout.c',
    'src/layout_config.c',
    'src/layout_select_mode.c',
    'src/lock_indicator.c',
    'src/lock_mode.c',
    'src/mark_assign_mode.c',
    'src/mark.c',
    'src/mark_select_mode.c',
    'src/maximized_state.c',
    'src/memory.c',
    'src/move_mode.c',
    'src/normal_mode.c',
    'src/output.c',
    'src/output_config.c',
    'src/pointer.c',
    'src/pointer_config.c',
    'src/position_config.c',
    'src/renderer.c',
    'src/resize_mode.c',
    'src/server.c',
    'src/sheet_assign_mode.c',
    'src/sheet.c',
    'src/split.c',
    'src/switch.c',
    'src/switch_config.c',
    'src/tile.c',
    'src/view.c',
    'src/view_config.c',
    'src/workspace.c',
    'src/xdg_view.c',
]

cc = meson.get_compiler('c')

m_dep = cc.find_library('m', required: false)

prefix     = get_option('prefix')
etc_prefix = get_option('etcprefix')
if etc_prefix == ''
    etc_prefix = prefix
endif

add_project_arguments(
    [
        '-D_GNU_SOURCE',
        '-DWLR_USE_UNSTABLE=1',
        '-DHIKARI_ETC_PREFIX=' + etc_prefix,
    ],
    language: 'c',
)

# Debug vs release build
if get_option('hikari_debug')
    add_project_arguments(['-g', '-O0', '-fsanitize=address'], language: 'c')
    add_project_link_arguments(['-fsanitize=address'], language: 'c')
else
    add_project_arguments(['-DNDEBUG'], language: 'c')
endif

# Feature flags
if get_option('xwayland')
    add_project_arguments(['-DHAVE_XWAYLAND=1'], language: 'c')
    srcs += ['src/xwayland_unmanaged_view.c', 'src/xwayland_view.c']
endif
if get_option('screencopy')
    add_project_arguments(['-DHAVE_SCREENCOPY=1'], language: 'c')
endif
if get_option('gammacontrol')
    add_project_arguments(['-DHAVE_GAMMACONTROL=1'], language: 'c')
endif
if get_option('layershell')
    add_project_arguments(['-DHAVE_LAYERSHELL=1'], language: 'c')
endif
if get_option('virtualinput')
    add_project_arguments(['-DHAVE_VIRTUAL_INPUT=1'], language: 'c')
endif

meson.add_dist_script('tools/dist-tarball.sh')

# Dependencies
wlroots    = dependency('wlroots-0.19')
wayland    = dependency('wayland-server')
wayland_protocols = dependency('wayland-protocols')
pixman     = dependency('pixman-1')
xkbcommon  = dependency('xkbcommon')
pangocairo = dependency('pangocairo')
cairo      = dependency('cairo')
libinput   = dependency('libinput')
libucl     = dependency('libucl')

all_deps = [
    wlroots,
    wayland,
    pixman,
    xkbcommon,
    pangocairo,
    cairo,
    libinput,
    libucl,
]

if m_dep.found()
    all_deps += m_dep
endif

if get_option('xwayland')
    xcb_icccm = dependency('xcb-icccm')
    all_deps += xcb_icccm
endif

# Protocol header generation
wayland_scanner = find_program('wayland-scanner')
wayland_protocols_datadir = wayland_protocols.get_variable('pkgdatadir')

xdg_shell_xml = wayland_protocols_datadir + '/stable/xdg-shell/xdg-shell.xml'
xdg_shell_header = custom_target(
    'xdg-shell-protocol.h',
    output: 'xdg-shell-protocol.h',
    command: [wayland_scanner, 'server-header', xdg_shell_xml, '@OUTPUT@'],
)

generated_srcs = [xdg_shell_header]

if get_option('layershell')
    layer_shell_xml = meson.project_source_root() + '/protocol/wlr-layer-shell-unstable-v1.xml'
    layer_shell_header = custom_target(
        'wlr-layer-shell-unstable-v1-protocol.h',
        output: 'wlr-layer-shell-unstable-v1-protocol.h',
        command: [wayland_scanner, 'server-header', layer_shell_xml, '@OUTPUT@'],
    )
    generated_srcs += layer_shell_header
endif

# version.h
version_h = configure_file(
    output: 'version.h',
    configuration: {
        'HIKARI_VERSION': '"' + meson.project_version() +'"'
    }
)
generated_srcs += version_h

include_dirs = [
    include_directories('.'),
    include_directories('include'),
]

executable('hikari',
    srcs + generated_srcs,
    include_directories: include_dirs,
    dependencies: all_deps,
    install: true,
)

# hikari-unlocker (requires PAM)
pam = cc.find_library('pam', required: true)
executable('hikari-unlocker',
    'hikari_unlocker.c',
    include_directories: include_dirs,
    dependencies: pam,
    install: true,
)

# Install config, wallpaper, desktop entry
install_data(
    'share/backgrounds/hikari/hikari_wallpaper.png',
    install_dir: get_option('datadir') / 'backgrounds/hikari',
)
install_data(
    'share/wayland-sessions/hikari.desktop',
    install_dir: get_option('datadir') / 'wayland-sessions',
)

# Config file with prefix substitution (Makefile uses bare "PREFIX" token)
hikari_conf = custom_target(
    'hikari.conf',
    input: 'etc/hikari/hikari.conf',
    output: 'hikari.conf',
    command: ['sed', 's,PREFIX,' + prefix + ',g', '@INPUT@'],
    capture: true,
    install: true,
    install_dir: etc_prefix + '/etc/hikari',
)

# Man page
pandoc = find_program('pandoc', required: false)
if pandoc.found()
    custom_target('hikari.1',
        input: 'share/man/man1/hikari.md',
        output: 'hikari.1',
        command: [
            pandoc,
            '-M', 'title:HIKARI(1) ' + meson.project_version() + ' | hikari - Wayland Compositor',
            '-s',
            '--to', 'man',
            '-o', '@OUTPUT@',
            '@INPUT@',
        ],
        install: true,
        install_dir: get_option('mandir') / 'man1',
    )
endif

# Summary
summary({
    'wlroots-0.19': wlroots,
    'wayland-server': wayland,
    'pixman-1': pixman,
    'xkbcommon': xkbcommon,
    'pangocairo': pangocairo,
    'cairo': cairo,
    'libinput': libinput,
    'libucl': libucl,
}, bool_yn: true, section: 'Dependencies')

summary({
    'XWayland support': get_option('xwayland'),
    'Screencopy support': get_option('screencopy'),
    'Gamma Control': get_option('gammacontrol'),
    'Layer Shell': get_option('layershell'),
    'Virtual Input': get_option('virtualinput'),
    'Debug build': get_option('hikari_debug'),
}, bool_yn: true, section: 'Flags')

summary({
    'Man pages': pandoc.found(),
}, bool_yn: true, section: 'Documentation')
