
noinst_LTLIBRARIES = libsystem-tray.la

AM_CPPFLAGS =							\
	$(NOTIFICATION_AREA_CFLAGS)				\
	-I$(srcdir)						\
	-I$(srcdir)/..						\
	-DMATELOCALEDIR=\""$(datadir)/locale"\"			\
	-DG_LOG_DOMAIN=\""notification-area-applet"\"		\
	$(DISABLE_DEPRECATED_CFLAGS)

AM_CFLAGS = $(WARN_CFLAGS)

libsystem_tray_la_SOURCES =	\
	fixedtip.h		\
	fixedtip.c		\
	na-marshal.c		\
	na-marshal.h		\
	na-tray.c		\
	na-tray.h		\
	na-tray-child.c		\
	na-tray-child.h		\
	na-tray-manager.c	\
	na-tray-manager.h

libsystem_tray_la_LIBADD =		\
	$(X_LIBS)			\
	$(NOTIFICATION_AREA_LIBS)

na-marshal.h: na-marshal.list $(GLIB_GENMARSHAL)
	$(AM_V_GEN)$(GLIB_GENMARSHAL) $< --header --prefix=_na_marshal > $@

na-marshal.c: na-marshal.list $(GLIB_GENMARSHAL)
	$(AM_V_GEN)echo "#include \"na-marshal.h\"" > $@ && \
	$(GLIB_GENMARSHAL) $< --body --prefix=_na_marshal >> $@

BUILT_SOURCES = na-marshal.c na-marshal.h

EXTRA_DIST =		\
	na-marshal.list

-include $(top_srcdir)/git.mk
