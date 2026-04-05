#!/usr/bin/bash

set -eo pipefail

# Use grouped output messages
infobegin() {
	echo "::group::${1}"
}
infoend() {
	echo "::endgroup::"
}

# Required packages on Ubuntu
requires=(
	ccache # Use ccache to speed up build
)

# https://git.launchpad.net/ubuntu/+source/mate-panel/tree/debian/control
requires+=(
	autoconf-archive
	autopoint
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
	libmate-menu-dev
	libmateweather-dev
	libpango1.0-dev
	libsm-dev
	libsoup-3.0-dev
	libwnck-3-dev
	libx11-dev
	libxrandr-dev
	make
	mate-common
	yelp-tools
)

infobegin "Update system"
apt-get update -y
infoend

infobegin "Install dependency packages"
env DEBIAN_FRONTEND=noninteractive \
	apt-get install --assume-yes \
	${requires[@]}
infoend
