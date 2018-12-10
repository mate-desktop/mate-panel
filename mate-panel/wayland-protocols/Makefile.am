if ENABLE_WAYLAND
all: wlr-layer-shell-unstable-v1-client-protocol.h wlr-layer-shell-unstable-v1-protocol.c xdg-shell-client-protocol.h xdg-shell-protocol.c

wlr-layer-shell-unstable-v1-client-protocol.h: wlr-layer-shell-unstable-v1.xml
	wayland-scanner -c client-header wlr-layer-shell-unstable-v1.xml wlr-layer-shell-unstable-v1-client-protocol.h

wlr-layer-shell-unstable-v1-protocol.c: wlr-layer-shell-unstable-v1.xml
	wayland-scanner -c private-code wlr-layer-shell-unstable-v1.xml wlr-layer-shell-unstable-v1-protocol.c

xdg-shell-client-protocol.h: xdg-shell.xml
	wayland-scanner -c client-header xdg-shell.xml xdg-shell-client-protocol.h

xdg-shell-protocol.c: xdg-shell.xml
	wayland-scanner -c private-code xdg-shell.xml xdg-shell-protocol.c
endif
