noinst_LTLIBRARIES =			\
	libmate-panel-applet-private.la	\
	libmate-panel-applet-private-mini.la

AM_CPPFLAGS =							\
	$(PANEL_CFLAGS)						\
	-I.							\
	-I$(srcdir)						\
	-I$(top_srcdir)/mate-panel				\
	-I$(top_builddir)/mate-panel				\
	-I$(top_builddir)/mate-panel/libmate-panel-applets-private	\
	-I$(top_builddir)/mate-panel/libpanel-util		\
	-DDATADIR=\""$(datadir)"\"				\
	-DMATE_PANEL_APPLETS_DIR=\"$(appletsdir)\"			\
	$(DISABLE_DEPRECATED_CFLAGS)

AM_CFLAGS = $(WARN_CFLAGS)

libmate_panel_applet_private_la_SOURCES =	\
	mate-panel-applets-manager-dbus.c	\
	mate-panel-applets-manager-dbus.h	\
	mate-panel-applet-container.c	\
	mate-panel-applet-container.h	\
	mate-panel-applet-frame-dbus.c	\
	mate-panel-applet-frame-dbus.h

libmate_panel_applet_private_mini_la_SOURCES =\
	mate-panel-applet-mini.c		\
	mate-panel-applets-manager-dbus.c	\
	mate-panel-applets-manager-dbus.h	\
	mate-panel-applet-container.c	\
	mate-panel-applet-container.h

-include $(top_srcdir)/git.mk
