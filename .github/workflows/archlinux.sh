#!/usr/bin/bash

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
	meson  # Used for meson build
)

# https://gitlab.archlinux.org/archlinux/packaging/packages/mate-panel
requires+=(
	autoconf-archive
	gcc
	git
	glib2-devel
	gobject-introspection
	gtk-doc
	gtk-layer-shell
	itstool
	libcanberra
	libmateweather
	libsm
	libwnck3
	libxfce4windowing
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
