#!/usr/bin/bash

# Use grouped output messages
infobegin() {
	echo "::group::${1}"
}
infoend() {
	echo "::endgroup::"
}

# Required packages on Debian
requires=(
	ccache # Use ccache to speed up build
	meson  # Used for meson build
)

# https://salsa.debian.org/debian-mate-team/mate-panel/-/blob/master/debian/control
requires+=(
	autoconf-archive
	autopoint
	gcc
	gir1.2-freedesktop
	git
	gobject-introspection
	gtk-doc-tools
	libatk1.0-dev
	libcairo2-dev
	libdconf-dev
	libgirepository1.0-dev
	libglib2.0-dev
	libgtk-3-dev
	libgtk-layer-shell-dev
	libice-dev
	libmate-desktop-dev
	libmate-menu-dev
	libmateweather-dev
	libpango1.0-dev
	libsm-dev
	libwnck-3-dev
	libx11-dev
	libxfce4windowing-0-dev
	libxrandr-dev
	lsb-release
	make
	mate-common
	yelp-tools
)

infobegin "Update system"
apt-get update -qq
infoend

infobegin "Install dependency packages"
env DEBIAN_FRONTEND=noninteractive \
	apt-get install --assume-yes \
	${requires[@]}
infoend
