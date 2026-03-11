#!/bin/sh
#
# hikari-version-str: emits the version of hikari which is building.
#		    If this is a release build, then the tag name is chomped
#		    to remove extraneous git information.
#
#		    If it's a developer build, it's left as-is.
#
#
#
# Intended to be called from meson.
set -e

HIKARI_RELEASE=no
VERSION=3.0.0

[ -d ".git" -a "HIKARI_RELEASE" = "no" ] || { echo "$VERSION" && exit 0 ; }

git describe --always --long --dirty --tags || echo "$VERSION"

