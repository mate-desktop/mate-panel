AM_CPPFLAGS =							\
	$(LIBMATE_PANEL_APPLET_CFLAGS)				\
	$(WNCKLET_CFLAGS)					\
	-I$(top_builddir)					\
	-I$(top_builddir)/applets/wncklet			\
	-I$(top_srcdir)/libmate-panel-applet				\
	-I$(top_builddir)/libmate-panel-applet			\
	-DMATELOCALEDIR=\""$(datadir)/locale"\"	\
	$(DISABLE_DEPRECATED_CFLAGS)

AM_CFLAGS = $(WARN_CFLAGS)

WNCKLET_SOURCES = \
	wncklet.c \
	wncklet.h \
	window-menu.c \
	window-menu.h \
	window-list.c \
	window-list.h \
	workspace-switcher.c \
	workspace-switcher.h \
	showdesktop.c \
	showdesktop.h \
	$(BUILT_SOURCES)

WNCKLET_LDADD =						\
	../../libmate-panel-applet/libmate-panel-applet-4.la	\
	$(WNCKLET_LIBS)					\
	$(LIBMATE_PANEL_APPLET_LIBS)

if WNCKLET_INPROCESS
APPLET_IN_PROCESS = true
APPLET_LOCATION   = $(pkglibdir)/libwnck-applet.so

wnck_appletlibdir = $(pkglibdir)
wnck_appletlib_LTLIBRARIES = libwnck-applet.la
libwnck_applet_la_SOURCES = $(WNCKLET_SOURCES)
libwnck_applet_la_LIBADD = $(WNCKLET_LDADD)
libwnck_applet_la_LDFLAGS = -module -avoid-version
libwnck_applet_la_CFLAGS = $(AM_CFLAGS)
else
APPLET_IN_PROCESS = false
APPLET_LOCATION   = $(libexecdir)/wnck-applet

libexec_PROGRAMS = wnck-applet
wnck_applet_SOURCES = $(WNCKLET_SOURCES)
wnck_applet_LDADD = $(WNCKLET_LDADD)
wnck_applet_CFLAGS = $(AM_CFLAGS)
endif

appletdir       = $(datadir)/mate-panel/applets
applet_in_files = org.mate.panel.Wncklet.mate-panel-applet.desktop.in
applet_DATA     = $(applet_in_files:.mate-panel-applet.desktop.in=.mate-panel-applet)

$(applet_in_files): $(applet_in_files).in Makefile
	$(AM_V_GEN)sed \
		-e "s|\@LOCATION\@|$(APPLET_LOCATION)|" \
		-e "s|\@IN_PROCESS\@|$(APPLET_IN_PROCESS)|" \
		-e "s|\@VERSION\@|$(PACKAGE_VERSION)|" \
		$< > $@

$(applet_DATA): $(applet_in_files)
	$(AM_V_GEN) $(MSGFMT) --desktop --keyword= --keyword=Name --keyword=Description --template $< -d $(top_srcdir)/po -o $@

if !WNCKLET_INPROCESS
servicedir       = $(datadir)/dbus-1/services
service_in_files = org.mate.panel.applet.WnckletFactory.service.in
service_DATA     = $(service_in_files:.service.in=.service)

org.mate.panel.applet.WnckletFactory.service: $(service_in_files)
	$(AM_V_GEN)sed \
		-e "s|\@LOCATION\@|$(APPLET_LOCATION)|" \
		$< > $@
endif

ui_FILES = \
	showdesktop-menu.xml \
	window-list-menu.xml \
	window-list.ui \
	window-menu-menu.xml \
	workspace-switcher-menu.xml \
	workspace-switcher.ui

wncklet-resources.c: wncklet.gresource.xml $(shell $(GLIB_COMPILE_RESOURCES) --sourcedir=$(srcdir) --generate-dependencies $(srcdir)/wncklet.gresource.xml)
	$(AM_V_GEN)$(GLIB_COMPILE_RESOURCES) --target=$@ --sourcedir=$(srcdir) --generate --c-name wncklet $<

wncklet-resources.h: wncklet.gresource.xml $(shell $(GLIB_COMPILE_RESOURCES) --sourcedir=$(srcdir) --generate-dependencies $(srcdir)/wncklet.gresource.xml)
	$(AM_V_GEN)$(GLIB_COMPILE_RESOURCES) --target=$@ --sourcedir=$(srcdir) --generate --c-name wncklet $<

BUILT_SOURCES = 		\
	wncklet-resources.c	\
	wncklet-resources.h

if HAVE_WINDOW_PREVIEWS
wncklet_gschemas_in = \
	org.mate.panel.applet.window-list.gschema.xml.in \
	org.mate.panel.applet.window-list-previews.gschema.xml.in \
	org.mate.panel.applet.workspace-switcher.gschema.xml.in
else
wncklet_gschemas_in = \
	org.mate.panel.applet.window-list.gschema.xml.in \
	org.mate.panel.applet.workspace-switcher.gschema.xml.in
endif
gsettings_SCHEMAS = $(wncklet_gschemas_in:.xml.in=.xml)

@GSETTINGS_RULES@

EXTRA_DIST = \
	org.mate.panel.Wncklet.mate-panel-applet.desktop.in.in \
	$(service_in_files) \
	$(wncklet_gschemas_in) \
	$(ui_FILES) \
	wncklet.gresource.xml

CLEANFILES = $(BUILT_SOURCES) $(applet_DATA) $(applet_in_files) $(service_DATA) $(gsettings_SCHEMAS)

-include $(top_srcdir)/git.mk
