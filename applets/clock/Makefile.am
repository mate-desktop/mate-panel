SUBDIRS = pixmaps

noinst_LTLIBRARIES = libsystem-timezone.la
noinst_PROGRAMS = test-system-timezone

AM_CPPFLAGS =				\
	$(TZ_CFLAGS)			\
	$(DISABLE_DEPRECATED_CFLAGS)

AM_CFLAGS = $(WARN_CFLAGS)

libsystem_timezone_la_SOURCES = \
	system-timezone.c	\
	system-timezone.h
libsystem_timezone_la_LIBADD = $(TZ_LIBS)

CLOCK_SOURCES = 		\
	calendar-window.c	\
	calendar-window.h	\
	clock.c			\
	clock.h			\
	clock-face.c		\
	clock-face.h		\
	clock-location.c	\
	clock-location.h	\
	clock-location-tile.c	\
	clock-location-tile.h	\
	clock-map.c		\
	clock-map.h		\
	clock-sunpos.c		\
	clock-sunpos.h		\
	clock-utils.c		\
	clock-utils.h		\
	set-timezone.c		\
	set-timezone.h		\
	$(BUILT_SOURCES)

CLOCK_CPPFLAGS =						\
	$(AM_CPPFLAGS)						\
	$(LIBMATE_PANEL_APPLET_CFLAGS)				\
	$(CLOCK_CFLAGS)						\
	-I$(srcdir)/../../libmate-panel-applet			\
	-I$(top_builddir)/libmate-panel-applet			\
	-DMATELOCALEDIR=\""$(datadir)/locale"\"	\
	-DMATEWEATHER_I_KNOW_THIS_IS_UNSTABLE

CLOCK_LDADD =						\
	../../libmate-panel-applet/libmate-panel-applet-4.la	\
	$(CLOCK_LIBS)					\
	$(LIBMATE_PANEL_APPLET_LIBS)				\
	libsystem-timezone.la				\
	-lm

test_system_timezone_SOURCES = 	\
	test-system-timezone.c
test_system_timezone_LDADD = libsystem-timezone.la

if CLOCK_INPROCESS
APPLET_IN_PROCESS = true
APPLET_LOCATION   = $(pkglibdir)/libclock-applet.so

clock_appletlibdir = $(pkglibdir)
clock_appletlib_LTLIBRARIES = libclock-applet.la
libclock_applet_la_SOURCES = $(CLOCK_SOURCES)
libclock_applet_la_CPPFLAGS = $(CLOCK_CPPFLAGS)
libclock_applet_la_LIBADD = $(CLOCK_LDADD)
libclock_applet_la_LDFLAGS = -module -avoid-version
libclock_applet_la_CFLAGS = $(AM_CFLAGS)
$(libclock_applet_la_OBJECTS): $(BUILT_SOURCES)
else
APPLET_IN_PROCESS = false
APPLET_LOCATION = $(libexecdir)/clock-applet

libexec_PROGRAMS = clock-applet
clock_applet_SOURCES = $(CLOCK_SOURCES)
clock_applet_CPPFLAGS = $(CLOCK_CPPFLAGS)
clock_applet_LDADD = $(CLOCK_LDADD)
clock_applet_CFLAGS = $(AM_CFLAGS)
$(clock_applet_OBJECTS): $(BUILT_SOURCES)
endif

clock-marshallers.c: clock-marshallers.list
	$(AM_V_GEN)glib-genmarshal --prefix _clock_marshal --header --body --internal $< > $@

clock-marshallers.h: clock-marshallers.list
	$(AM_V_GEN)glib-genmarshal --prefix _clock_marshal --header --internal $< > $@

clock_enum_headers =					\
	$(top_srcdir)/applets/clock/clock-utils.h

clock-typebuiltins.c: @REBUILD@ $(clock_enum_headers)
	$(AM_V_GEN)glib-mkenums \
			--fhead "#include <glib-object.h>\n" \
			--fhead "#include \"clock-typebuiltins.h\"\n\n" \
			--fprod "\n/* enumerations from \"@filename@\" */" \
			--fprod "\n#include \"@filename@\"\n" \
			--vhead "static const GEnumValue _@enum_name@_values[] = {" \
			--vprod "  { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" }," \
			--vtail "  { 0, NULL, NULL }\n};\n\n" \
			--vtail "GType\n@enum_name@_get_type (void)\n{\n" \
			--vtail "  static GType type = 0;\n\n" \
			--vtail "  if (!type)\n" \
			--vtail "    type = g_enum_register_static (\"@EnumName@\", _@enum_name@_values);\n\n" \
			--vtail "  return type;\n}\n\n" \
		$(clock_enum_headers) > $@

clock-typebuiltins.h: @REBUILD@ $(clock_enum_headers)
	$(AM_V_GEN)glib-mkenums \
			--fhead "#ifndef __CLOCK_TYPEBUILTINS_H__\n" \
			--fhead "#define __CLOCK_TYPEBUILTINS_H__ 1\n\n" \
			--fhead "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n" \
			--ftail "#ifdef __cplusplus\n}\n#endif\n\n" \
			--ftail "#endif /* __CLOCK_TYPEBUILTINS_H__ */\n" \
			--fprod "\n/* --- @filename@ --- */" \
			--eprod "#define CLOCK_TYPE_@ENUMSHORT@ @enum_name@_get_type()\n" \
			--eprod "GType @enum_name@_get_type (void);\n" \
		$(clock_enum_headers) > $@

BUILT_SOURCES =					\
	clock-marshallers.c			\
	clock-marshallers.h			\
	clock-resources.c			\
	clock-resources.h			\
	clock-typebuiltins.c			\
	clock-typebuiltins.h

appletdir       = $(datadir)/mate-panel/applets
applet_in_files = org.mate.panel.ClockApplet.mate-panel-applet.desktop.in
applet_DATA     = $(applet_in_files:.mate-panel-applet.desktop.in=.mate-panel-applet)

$(applet_in_files): $(applet_in_files).in Makefile
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

org.mate.panel.applet.ClockAppletFactory.service: $(service_in_files)
	$(AM_V_GEN)sed \
		-e "s|\@LOCATION\@|$(APPLET_LOCATION)|" \
		$< > $@
endif

ui_FILES =	\
	clock.ui \
	clock-menu.xml

clock-resources.c: clock.gresource.xml $(shell $(GLIB_COMPILE_RESOURCES) --sourcedir=$(srcdir) --generate-dependencies $(srcdir)/clock.gresource.xml)
	$(AM_V_GEN)$(GLIB_COMPILE_RESOURCES) --target=$@ --sourcedir=$(srcdir) --generate --c-name clock $<

clock-resources.h: clock.gresource.xml $(shell $(GLIB_COMPILE_RESOURCES) --sourcedir=$(srcdir) --generate-dependencies $(srcdir)/clock.gresource.xml)
	$(AM_V_GEN)$(GLIB_COMPILE_RESOURCES) --target=$@ --sourcedir=$(srcdir) --generate --c-name clock $<

clock_gschemas_in = org.mate.panel.applet.clock.gschema.xml.in
gsettings_SCHEMAS = $(clock_gschemas_in:.xml.in=.xml)

@GSETTINGS_RULES@

EXTRA_DIST =						\
	org.mate.panel.ClockApplet.mate-panel-applet.desktop.in.in	\
	$(clock_gschemas_in)				\
	$(ui_FILES)					\
	clock.gresource.xml				\
	clock-marshallers.list				\
	$(service_in_files)

CLEANFILES =			\
	$(BUILT_SOURCES)	\
	$(applet_DATA) 		\
	$(applet_in_files) 	\
	$(service_DATA)		\
	$(polkit_DATA)		\
	$(dbus_services_DATA)	\
	$(gsettings_SCHEMAS)

-include $(top_srcdir)/git.mk
