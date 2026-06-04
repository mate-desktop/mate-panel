#!/usr/bin/bash

set -eo pipefail

# Use grouped output messages
infobegin() {
	echo "::group::${1}"
}
infoend() {
	echo "::endgroup::"
}

# Required packages on Fedora
requires=(
	ccache # Use ccache to speed up build
)

# https://src.fedoraproject.org/cgit/rpms/mate-panel.git
requires+=(
	autoconf-archive
	desktop-file-utils
	gcc
	git
	gobject-introspection-devel
	gtk3-devel
	gtk-layer-shell-devel
	libSM-devel
	libmateweather-devel
	libwnck3-devel
	make
	mate-common
	mate-menus-devel
	redhat-rpm-config
	yelp-tools
)

infobegin "Update system"
dnf update -y
infoend

infobegin "Install dependency packages"
dnf install -y ${requires[@]}
infoend
