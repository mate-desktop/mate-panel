SUBDIRS = pixmaps
NULL =

appletdir          = $(datadir)/mate-panel/applets
applet_in_in_files = org.mate.panel.ClockApplet.mate-panel-applet.desktop.in.in
applet_in_files    = $(applet_in_in_files:.desktop.in.in=.desktop.in)
applet_DATA        = $(applet_in_files:.mate-panel-applet.desktop.in=.mate-panel-applet)

$(applet_in_files): $(applet_in_in_files) Makefile
	$(AM_V_GEN)sed \
		-e "s|\@LOCATION\@|$(APPLET_LOCATION)|" \
		-e "s|\@IN_PROCESS\@|$(APPLET_IN_PROCESS)|" \
		-e "s|\@VERSION\@|$(PACKAGE_VERSION)|" \
		$< > $@

$(applet_DATA): $(applet_in_files)
	$(AM_V_GEN) $(MSGFMT) --desktop --keyword= --keyword=Name --keyword=Description --template $< -d $(top_srcdir)/po -o $@

if !CLOCK_INPROCESS
servicedir       = $(datadir)/dbus-1/services
service_in_files = org.mate.panel.applet.ClockAppletFactory.service.in
service_DATA     = $(service_in_files:.service.in=.service)

$(service_DATA): $(service_in_files)
	$(AM_V_GEN)sed \
		-e "s|\@LOCATION\@|$(APPLET_LOCATION)|" \
		$< > $@
endif

ui_FILES = clock.ui clock-menu.xml

clock_gschemas_in = org.mate.panel.applet.clock.gschema.xml.in
gsettings_SCHEMAS = $(clock_gschemas_in:.xml.in=.xml)

@GSETTINGS_RULES@

EXTRA_DIST =			\
	$(applet_in_in_files)	\
	$(clock_gschemas_in_in)	\
	$(service_in_files)	\
	$(ui_FILES)		\
	clock.gresource.xml	\
	clock-marshallers.list	\
	$(NULL)

CLEANFILES =			\
	$(applet_DATA) 		\
	$(applet_in_files) 	\
	$(dbus_services_DATA)	\
	$(polkit_DATA)		\
	$(service_DATA)		\
	$(NULL)

DISTCLEANFILES =		\
	$(gsettings_SCHEMAS)	\
	$(NULL)

-include $(top_srcdir)/git.mk
