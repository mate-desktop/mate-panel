SUBDIRS =			\
	po				\
	mate-panel		\
	libmate-panel-applet		\
	applets			\
	doc			\
	help			\
	icons			\
	data			\
	man

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

DISTCHECK_CONFIGURE_FLAGS = \
	--enable-gtk-doc \
	--disable-introspection \
	--enable-compile-warnings=no \
	CFLAGS='-Wno-deprecated-declarations'

EXTRA_DIST = \
	autogen.sh		\
	COPYING-DOCS		\
	HACKING \
	meson.build \
	meson_options.txt \
	applets/meson.build \
	applets/clock/meson.build \
	applets/fish/meson.build \
	applets/notification_area/meson.build \
	applets/notification_area/libstatus-notifier-watcher/meson.build \
	applets/notification_area/status-notifier/meson.build \
	applets/notification_area/system-tray/meson.build \
	applets/wncklet/meson.build \
	data/meson.build \
	doc/meson.build \
	doc/reference/meson.build \
	doc/reference/mate-panel-applet/meson.build \
	help/meson.build \
	help/mate-clock/meson.build \
	help/mate-fish/meson.build \
	icons/meson.build \
	libmate-panel-applet/meson.build \
	man/meson.build \
	mate-panel/meson.build \
	mate-panel/libmate-panel-applet-private/meson.build \
	mate-panel/libpanel-util/meson.build \
	po/meson.build

CLEANFILES = \
	config.h

MAINTAINERCLEANFILES = \
	$(srcdir)/INSTALL \
	$(srcdir)/aclocal.m4 \
	$(srcdir)/compile \
	$(srcdir)/config.guess \
	$(srcdir)/config.h.in \
	$(srcdir)/config.sub \
	$(srcdir)/depcomp \
	$(srcdir)/install-sh \
	$(srcdir)/ltmain.sh \
	$(srcdir)/missing \
	$(srcdir)/mkinstalldirs \
	`find "$(srcdir)" -type f -name Makefile.in -print` \
	$(srcdir)/configure \
	$(srcdir)/gtk-doc.make \
	$(srcdir)/m4/gtk-doc.m4 \
	$(srcdir)/m4/libtool.m4 \
	$(srcdir)/m4/ltoptions.m4 \
	$(srcdir)/m4/ltsugar.m4 \
	$(srcdir)/m4/ltversion.m4 \
	$(srcdir)/m4/lt~obsolete.m4

# Build ChangeLog from GIT  history
ChangeLog:
	$(AM_V_GEN) if test -d $(top_srcdir)/.git; then \
		GIT_DIR="$(top_srcdir)/.git" git log --stat > $@; \
	fi

dist: ChangeLog

.PHONY: ChangeLog

-include $(top_srcdir)/git.mk
