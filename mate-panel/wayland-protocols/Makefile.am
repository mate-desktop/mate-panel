if ENABLE_WAYLAND
all: wlr-layer-shell-unstable-v1-client-protocol.h wlr-layer-shell-unstable-v1-protocol.c xdg-shell-client-protocol.h xdg-shell-protocol.c

wlr-layer-shell-unstable-v1-client-protocol.h: wlr-layer-shell-unstable-v1.xml
	wayland-scanner -c client-header wlr-layer-shell-unstable-v1.xml wlr-layer-shell-unstable-v1-client-protocol.h

# 'code' is depricated, and can be replaced with 'private-code' when all platforms have a new enough wayland-scanner
wlr-layer-shell-unstable-v1-protocol.c: wlr-layer-shell-unstable-v1.xml
	wayland-scanner -c code wlr-layer-shell-unstable-v1.xml wlr-layer-shell-unstable-v1-protocol.c

xdg-shell-client-protocol.h: xdg-shell.xml
	wayland-scanner -c client-header xdg-shell.xml xdg-shell-client-protocol.h

# 'code' is depricated, and can be replaced with 'private-code' when all platforms have a new enough wayland-scanner
xdg-shell-protocol.c: xdg-shell.xml
	wayland-scanner -c code xdg-shell.xml xdg-shell-protocol.c
endif

clean:
	rm -f wlr-layer-shell-unstable-v1-client-protocol.h
	rm -f wlr-layer-shell-unstable-v1-protocol.c
	rm -f xdg-shell-client-protocol.h
	rm -f xdg-shell-protocol.c
