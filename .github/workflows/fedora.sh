#!/usr/bin/bash

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
	meson  # Used for meson build
)

# https://src.fedoraproject.org/rpms/mate-panel/blob/rawhide/f/mate-panel.spec
requires+=(
	autoconf-archive
	dconf-devel
	desktop-file-utils
	gcc
	gettext-devel
	git
	gobject-introspection-devel
	gtk-doc
	gtk-layer-shell-devel
	gtk3-devel
	libSM-devel
	libXrandr-devel
	libmateweather-devel
	libwnck3-devel
	libxfce4windowing-devel
	make
	mate-common
	mate-desktop-devel
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
