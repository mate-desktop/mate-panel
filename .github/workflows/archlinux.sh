#!/usr/bin/bash

set -eo pipefail

# Use grouped output messages
infobegin() {
	echo "::group::${1}"
}
infoend() {
	echo "::endgroup::"
}

# Required packages on Archlinux
requires=(
	ccache # Use ccache to speed up build
	clang  # Build with clang on Archlinux
)

# https://gitlab.archlinux.org/archlinux/packaging/packages/mate-panel
requires+=(
	autoconf-archive
	dbus-glib
	dconf-editor
	gcc
	gettext
	git
	glib2-devel
	gobject-introspection
	gtk-layer-shell
	itstool
	libcanberra
	libmateweather
	libsm
	libwnck3
	make
	mate-common
	mate-desktop
	mate-menus
	which
	yelp-tools
)

infobegin "Update system"
pacman --noconfirm -Syu
infoend

infobegin "Install dependency packages"
pacman --noconfirm -S ${requires[@]}
infoend
