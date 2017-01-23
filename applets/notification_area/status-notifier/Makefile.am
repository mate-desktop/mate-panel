NULL =

noinst_LTLIBRARIES = libstatus-notifier.la

AM_CPPFLAGS =							\
	$(NOTIFICATION_AREA_CFLAGS)				\
	$(LIBMATE_PANEL_APPLET_CFLAGS)				\
	-I$(srcdir)						\
	-I$(srcdir)/..						\
	-DMATELOCALEDIR=\""$(datadir)/locale"\"			\
	-DG_LOG_DOMAIN=\""notification-area-applet"\"		\
	$(DISABLE_DEPRECATED_CFLAGS)

AM_CFLAGS = $(WARN_CFLAGS)

libstatus_notifier_la_SOURCES =		\
	sn-dbus-menu.c			\
	sn-dbus-menu.h			\
	sn-dbus-menu-item.c		\
	sn-dbus-menu-item.h		\
	sn-flat-button.c		\
	sn-flat-button.h		\
	sn-host-v0.c			\
	sn-host-v0.h			\
	sn-item.c			\
	sn-item.h			\
	sn-item-v0.c			\
	sn-item-v0.h			\
	$(BUILT_SOURCES)		\
	$(NULL)

libstatus_notifier_la_LIBADD =				\
	$(LIBM)						\
	$(NOTIFICATION_AREA_LIBS)			\
	$(NULL)

sn-dbus-menu-gen.h:
sn-dbus-menu-gen.c: com.canonical.dbusmenu.xml
	$(AM_V_GEN) $(GDBUS_CODEGEN) --c-namespace Sn \
		--generate-c-code sn-dbus-menu-gen \
		$(srcdir)/com.canonical.dbusmenu.xml

sn-host-v0-gen.h:
sn-host-v0-gen.c: org.kde.StatusNotifierHost.xml
	$(AM_V_GEN) $(GDBUS_CODEGEN) --c-namespace Sn \
		--generate-c-code sn-host-v0-gen \
		$(srcdir)/org.kde.StatusNotifierHost.xml

sn-item-v0-gen.h:
sn-item-v0-gen.c: org.kde.StatusNotifierItem.xml
	$(AM_V_GEN) $(GDBUS_CODEGEN) --c-namespace Sn \
		--generate-c-code sn-item-v0-gen \
		$(srcdir)/org.kde.StatusNotifierItem.xml

sn-watcher-v0-gen.h:
sn-watcher-v0-gen.c: org.kde.StatusNotifierWatcher.xml
	$(AM_V_GEN) $(GDBUS_CODEGEN) --c-namespace Sn \
		--generate-c-code sn-watcher-v0-gen \
		$(srcdir)/org.kde.StatusNotifierWatcher.xml

BUILT_SOURCES =			\
	sn-dbus-menu-gen.c	\
	sn-dbus-menu-gen.h	\
	sn-host-v0-gen.c	\
	sn-host-v0-gen.h	\
	sn-item-v0-gen.c	\
	sn-item-v0-gen.h	\
	sn-watcher-v0-gen.c	\
	sn-watcher-v0-gen.h	\
	$(NULL)

EXTRA_DIST =					\
	com.canonical.dbusmenu.xml		\
	org.kde.StatusNotifierHost.xml		\
	org.kde.StatusNotifierItem.xml		\
	org.kde.StatusNotifierWatcher.xml	\
	$(NULL)

CLEANFILES =			\
	$(BUILT_SOURCES)	\
	$(NULL)

-include $(top_srcdir)/git.mk
