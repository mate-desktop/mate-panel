NULL =

noinst_LTLIBRARIES = \
	libstatus-notifier-watcher.la \
	$(NULL)

AM_CPPFLAGS =							\
	$(NOTIFICATION_AREA_CFLAGS)				\
	-DG_LOG_DOMAIN=\"status-notifier-watcher\"		\
	$(DISABLE_DEPRECATED_CFLAGS)

AM_CFLAGS = $(WARN_CFLAGS)

libstatus_notifier_watcher_la_SOURCES = \
	gf-sn-watcher-v0.c \
	gf-sn-watcher-v0.h \
	gf-status-notifier-watcher.c \
	gf-status-notifier-watcher.h \
	$(BUILT_SOURCES) \
	$(NULL)

libstatus_notifier_watcher_la_LIBADD =	\
	$(NOTIFICATION_AREA_LIBS)	\
	$(NULL)

gf-sn-watcher-v0-gen.h:
gf-sn-watcher-v0-gen.c: org.kde.StatusNotifierWatcher.xml
	$(AM_V_GEN) $(GDBUS_CODEGEN) --c-namespace Gf \
		--generate-c-code gf-sn-watcher-v0-gen \
		$(srcdir)/org.kde.StatusNotifierWatcher.xml

BUILT_SOURCES = \
	gf-sn-watcher-v0-gen.c \
	gf-sn-watcher-v0-gen.h \
	$(NULL)

EXTRA_DIST = \
	org.kde.StatusNotifierWatcher.xml \
	$(NULL)

CLEANFILES = \
	$(BUILT_SOURCES) \
	$(NULL)

-include $(top_srcdir)/git.mk
