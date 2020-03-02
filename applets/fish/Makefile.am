AM_CPPFLAGS = \
	$(LIBMATE_PANEL_APPLET_CFLAGS) \
	$(FISH_CFLAGS) \
	-I$(srcdir) \
	-I$(srcdir)/../../libmate-panel-applet \
	-I$(top_builddir)/libmate-panel-applet \
	-DMATELOCALEDIR=\""$(datadir)/locale"\" \
	-DFISH_ICONDIR=\""$(fishbitmapsdir)"\" \
	$(DISABLE_DEPRECATED_CFLAGS)

AM_CFLAGS = $(WARN_CFLAGS)

FISH_SOURCES = \
	fish.c		\
	$(BUILT_SOURCES)

FISH_LDADD = \
	../../libmate-panel-applet/libmate-panel-applet-4.la \
	$(FISH_LIBS) \
	$(LIBMATE_PANEL_APPLET_LIBS)

if FISH_INPROCESS
APPLET_IN_PROCESS = true
APPLET_LOCATION   = $(pkglibdir)/libfish-applet.so

fish_applet_libdir = $(pkglibdir)
fish_applet_lib_LTLIBRARIES = libfish-applet.la
libfish_applet_la_SOURCES = $(FISH_SOURCES)
libfish_applet_la_LIBADD = $(FISH_LDADD)
libfish_applet_la_LDFLAGS = -module -avoid-version
libfish_applet_la_CFLAGS = $(AM_CFLAGS)
else
APPLET_IN_PROCESS = false
APPLET_LOCATION = $(libexecdir)/fish-applet

libexec_PROGRAMS = fish-applet
fish_applet_SOURCES = $(FISH_SOURCES)
fish_applet_LDADD = $(FISH_LDADD)
fish_applet_CFLAGS = $(AM_CFLAGS)
endif

appletdir       = $(datadir)/mate-panel/applets
applet_in_files = org.mate.panel.FishApplet.mate-panel-applet.desktop.in
applet_DATA     = $(applet_in_files:.mate-panel-applet.desktop.in=.mate-panel-applet)

$(applet_in_files): $(applet_in_files).in Makefile
	$(AM_V_GEN)sed \
		-e "s|\@LOCATION\@|$(APPLET_LOCATION)|" \
		-e "s|\@IN_PROCESS\@|$(APPLET_IN_PROCESS)|" \
		-e "s|\@VERSION\@|$(PACKAGE_VERSION)|" \
		$< > $@

$(applet_DATA): $(applet_in_files)
	$(AM_V_GEN) $(MSGFMT) --desktop --keyword= --keyword=Name --keyword=Description --template $< -d $(top_srcdir)/po -o $@

if !FISH_INPROCESS
servicedir       = $(datadir)/dbus-1/services
service_in_files = org.mate.panel.applet.FishAppletFactory.service.in
service_DATA     = $(service_in_files:.service.in=.service)

org.mate.panel.applet.FishAppletFactory.service: $(service_in_files)
	$(AM_V_GEN)sed \
		-e "s|\@LOCATION\@|$(APPLET_LOCATION)|" \
		$< > $@
endif

fishbitmapsdir = $(datadir)/mate-panel/fish
fishbitmaps_DATA = \
	wanda.png \
	fishanim.png \
	oldwanda.png \
	footguy.png \
	monkey.png

ui_FILES = \
	fish.ui	\
	fish-menu.xml

fish-resources.c: fish.gresource.xml $(shell $(GLIB_COMPILE_RESOURCES) --sourcedir=$(srcdir) --generate-dependencies $(srcdir)/fish.gresource.xml)
	$(AM_V_GEN)$(GLIB_COMPILE_RESOURCES) --target=$@ --sourcedir=$(srcdir) --generate --c-name fish $<

fish-resources.h: fish.gresource.xml $(shell $(GLIB_COMPILE_RESOURCES) --sourcedir=$(srcdir) --generate-dependencies $(srcdir)/fish.gresource.xml)
	$(AM_V_GEN)$(GLIB_COMPILE_RESOURCES) --target=$@ --sourcedir=$(srcdir) --generate --c-name fish $<

BUILT_SOURCES = 		\
	fish-resources.c	\
	fish-resources.h

fish_gschemas_in = org.mate.panel.applet.fish.gschema.xml.in
gsettings_SCHEMAS = $(fish_gschemas_in:.xml.in=.xml)
@GSETTINGS_RULES@

EXTRA_DIST = \
	wanda.svg \
	$(fishbitmaps_DATA) \
	org.mate.panel.FishApplet.mate-panel-applet.desktop.in.in \
	$(service_in_files) \
	$(ui_FILES)	\
	fish.gresource.xml \
	$(fish_gschemas_in)

CLEANFILES = $(BUILT_SOURCES) $(applet_DATA) $(applet_in_files) $(service_DATA) $(gsettings_SCHEMAS)

-include $(top_srcdir)/git.mk
