noinst_LTLIBRARIES = libsystem-timezone.la
noinst_PROGRAMS = test-system-timezone

AM_CPPFLAGS =			\
	$(TZ_CFLAGS)		\
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
	set-timezone.h

CLOCK_CPPFLAGS =				\
	$(AM_CPPFLAGS)				\
	$(LIBMATE_PANEL_APPLET_CFLAGS)		\
	$(CLOCK_CFLAGS)				\
	-I$(top_srcdir)				\
	-I$(top_srcdir)/libmate-panel-applet	\
	-DMATELOCALEDIR=\""$(datadir)/locale"\"	\
	-DMATEWEATHER_I_KNOW_THIS_IS_UNSTABLE

CLOCK_LDADD =				\
	../../../libmate-panel-applet/libmate-panel-applet-4.la \
	$(CLOCK_LIBS)			\
	$(LIBMATE_PANEL_APPLET_LIBS)	\
	libsystem-timezone.la		\
	-lm

test_system_timezone_SOURCES = test-system-timezone.c
test_system_timezone_LDADD = libsystem-timezone.la

if CLOCK_INPROCESS
APPLET_IN_PROCESS = true
APPLET_LOCATION   = $(pkglibdir)/libclock-applet.so
clock_appletlibdir = $(pkglibdir)
clock_appletlib_LTLIBRARIES = libclock-applet.la
libclock_applet_la_SOURCES = $(CLOCK_SOURCES)
nodist_libclock_applet_la_SOURCES = $(BUILT_SOURCES)
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
nodist_clock_applet_SOURCES = $(BUILT_SOURCES)
clock_applet_CPPFLAGS = $(CLOCK_CPPFLAGS)
clock_applet_LDADD = $(CLOCK_LDADD)
clock_applet_CFLAGS = $(AM_CFLAGS)
$(clock_applet_OBJECTS): $(BUILT_SOURCES)
endif

clock-marshallers.c: $(top_srcdir)/applets/clock/data/clock-marshallers.list
	$(AM_V_GEN)glib-genmarshal --prefix _clock_marshal --header --body --internal $< > $@

clock-marshallers.h: $(top_srcdir)/applets/clock/data/clock-marshallers.list
	$(AM_V_GEN)glib-genmarshal --prefix _clock_marshal --header --internal $< > $@

clock_enum_headers = $(top_srcdir)/applets/clock/src/clock-utils.h

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

BUILT_SOURCES =			\
	clock-marshallers.c	\
	clock-marshallers.h	\
	clock-resources.c	\
	clock-resources.h	\
	clock-typebuiltins.c	\
	clock-typebuiltins.h

clock-resources.c: $(top_srcdir)/applets/clock/data/clock.gresource.xml $(shell $(GLIB_COMPILE_RESOURCES) --sourcedir=$(top_srcdir)/applets/clock/data --generate-dependencies $(top_srcdir)/applets/clock/data/clock.gresource.xml)
	$(AM_V_GEN)$(GLIB_COMPILE_RESOURCES) --target=$@ --sourcedir=$(top_srcdir)/applets/clock/data --generate --c-name clock $<

clock-resources.h: $(top_srcdir)/applets/clock/data/clock.gresource.xml $(shell $(GLIB_COMPILE_RESOURCES) --sourcedir=$(top_srcdir)/applets/clock/data --generate-dependencies $(top_srcdir)/applets/clock/data/clock.gresource.xml)
	$(AM_V_GEN)$(GLIB_COMPILE_RESOURCES) --target=$@ --sourcedir=$(top_srcdir)/applets/clock/data --generate --c-name clock $<

CLEANFILES =			\
	$(BUILT_SOURCES)	\
	$(service_DATA)		\
	$(polkit_DATA)		\
	$(gsettings_SCHEMAS)

-include $(top_srcdir)/git.mk
