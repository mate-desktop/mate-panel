#!/usr/bin/bash

set -e
set -o pipefail

CPUS=$(grep processor /proc/cpuinfo | wc -l)

# Use grouped output messages
infobegin() {
	echo "::group::${1}"
}
infoend() {
	echo "::endgroup::"
}

if [ -f autogen.sh ]; then
	infobegin "Configure (autotools)"
	NOCONFIGURE=1 ./autogen.sh
	./configure --prefix=/usr --enable-compile-warnings=maximum || {
		cat config.log
		exit 1
	}
	infoend

	infobegin "Build (autotools)"
	make -j ${CPUS}
	infoend

	infobegin "Check (autotools)"
	make -j ${CPUS} check || {
		true
	}
	infoend

	infobegin "Distcheck (autotools)"
	make -j ${CPUS} distcheck
	infoend
fi
