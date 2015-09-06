AM_CPPFLAGS = \
	$(LIBMATE_PANEL_APPLET_CFLAGS) \
	$(FISH_CFLAGS) \
	-I$(srcdir) \
	-I$(srcdir)/../../libmate-panel-applet \
	-I$(top_builddir)/libmate-panel-applet \
	-DMATELOCALEDIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	-DFISH_ICONDIR=\""$(fishbitmapsdir)"\" \
	-DFISH_BUILDERDIR=\""$(uidir)"\" \
	-DFISH_MENU_UI_DIR=\""$(xmluidir)"\" \
	$(DISABLE_DEPRECATED_CFLAGS)

AM_CFLAGS = $(WARN_CFLAGS)

FISH_SOURCES = fish.c

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
applet_in_files = org.mate.panel.FishApplet.mate-panel-applet.in
applet_DATA     = $(applet_in_files:.mate-panel-applet.in=.mate-panel-applet)

$(applet_in_files): $(applet_in_files).in Makefile
	$(AM_V_GEN)sed \
		-e "s|\@LOCATION\@|$(APPLET_LOCATION)|" \
		-e "s|\@IN_PROCESS\@|$(APPLET_IN_PROCESS)|" \
		-e "s|\@VERSION\@|$(PACKAGE_VERSION)|" \
		$< > $@

@PANEL_INTLTOOL_MATE_PANEL_APPLET_RULE@

if !FISH_INPROCESS
servicedir       = $(datadir)/dbus-1/services
service_in_files = org.mate.panel.applet.FishAppletFactory.service.in
service_DATA     = $(service_in_files:.service.in=.service)

org.mate.panel.applet.FishAppletFactory.service: $(service_in_files)
	$(AM_V_GEN)sed \
		-e "s|\@LOCATION\@|$(APPLET_LOCATION)|" \
		$< > $@
endif

fishbitmapsdir = $(datadir)/mate-panel/pixmaps
fishbitmaps_DATA = \
	wanda.png \
	fishanim.png \
	oldwanda.png \
	footguy.png \
	monkey.png

uidir = $(datadir)/mate-panel/ui
ui_DATA = fish.ui

xmluidir = $(datadir)/mate-panel/ui
xmlui_DATA = fish-menu.xml

fish_gschemas_in = org.mate.panel.applet.fish.gschema.xml.in
gsettings_SCHEMAS = $(fish_gschemas_in:.xml.in=.xml)
@INTLTOOL_XML_NOMERGE_RULE@
@GSETTINGS_RULES@

EXTRA_DIST = \
	wanda.svg \
	$(fishbitmaps_DATA) \
	org.mate.panel.FishApplet.mate-panel-applet.in.in \
	$(service_in_files) \
	$(ui_DATA) \
	$(xmlui_DATA) \
	$(fish_gschemas_in)

CLEANFILES = $(applet_DATA) $(applet_DATA).in $(service_DATA) $(gsettings_SCHEMAS)

-include $(top_srcdir)/git.mk
