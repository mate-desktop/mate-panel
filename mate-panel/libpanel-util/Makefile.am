noinst_LTLIBRARIES = libpanel-util.la

AM_CPPFLAGS =							\
	$(PANEL_CFLAGS)						\
	$(DCONF_CFLAGS)						\
	-I.							\
	-I$(srcdir)						\
	-I$(top_builddir)/mate-panel/libpanel-util		\
	-DDATADIR=\""$(datadir)"\"				\
	$(DISABLE_DEPRECATED_CFLAGS)

AM_CFLAGS = $(WARN_CFLAGS)

libpanel_util_la_SOURCES =		\
	panel-cleanup.c			\
	panel-cleanup.h			\
	panel-color.c			\
	panel-color.h			\
	panel-error.c			\
	panel-error.h			\
	panel-glib.c			\
	panel-glib.h			\
	panel-gtk.c			\
	panel-gtk.h			\
	panel-icon-chooser.c		\
	panel-icon-chooser.h		\
	panel-keyfile.c			\
	panel-keyfile.h			\
	panel-launch.c			\
	panel-launch.h			\
	panel-list.c			\
	panel-list.h			\
	panel-session-manager.c		\
	panel-session-manager.h		\
	panel-show.c			\
	panel-show.h			\
	panel-xdg.c			\
	panel-xdg.h

-include $(top_srcdir)/git.mk
