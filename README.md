# Hikari - Wayland Compositor

## Description

_hikari_ is a stacking Wayland compositor with additional tiling capabilities,
it is heavily inspired by the Calm Window manager (cwm(1)). Its core concepts
are *views*, *groups*, *sheets* and the *workspace*.

The workspace is the set of views that are currently visible.

A sheet is a collection of views, each view can only be a member of a single
sheet. Switching between sheets will replace the current content of the
workspace with all the views that are a member of the selected sheet. _hikari_
has 9 general purpose sheets that correspond to the numbers **1** to **9** and a
special purpose sheet **0**. Views that are a member of sheet **0** will
always be visible but stacked below the views of the selected sheet.

Groups are a bit more fine grained than sheets. Like sheets, groups are a
collection of views. Unlike sheets you can have a arbitrary number of groups
and each group can have an arbitrary name. Views from one group can be spread
among all available sheets. Some operations act on entire groups rather than
individual views.

# Note...

This is a fork from the original (hikari
repository)[https://hub.darcs.net/raichoo/hikari) which doesn't seem to be
active any longer.

Although this work was originally done in the darcs VCS, all future work is
now centred on Codeberg, and hence it is expected that PRs, Issues, etc., are
logged there in terms of this repo and ideally future hikari development as
well.

# Installation

# FreeBSD

For FreeBSD-specific instruction see: [FreeBSD Installation Instructions](README-FreeBSD.md)

# Linux/Everything Else

## Dependencies

* wlroots
* pango
* cairo
* libinput
* xkbcommon
* pixman
* libucl
* evdev-proto
* XWayland (optional, runtime dependency)
* pandoc (optional, needed to build man pages)

Build Environment: Meson

## Compiling and Installing

```
% meson setup build
% meson compile -C build && meson install -C build
```

For specific compile-time options, see `meson_options.txt` although  many of
these autodetect based on what's installed already

# Community

The `hikari` community gears to be inclusive and welcoming to everyone, this is
why we chose to adhere to the [Geekfeminism Code of
Conduct](https://hikari.acmelabs.space/coc.html).

If you care to be a part of our community, please join our Matrix chat at
`#hikari:acmelabs.space`, or on `#hikari` IRC on libera.chat

# Contributing

Please make sure you use `clang-format` with the accompanying `.clang-format`
configuration before submitting any patches.

Patches, PRs, Issues, etc., should all be reported to the codeberg repository
where this project is being managed:

https://codeberg.org/thomasadam/hikari
