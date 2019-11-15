lib_LTLIBRARIES = libmate-panel-applet-4.la
noinst_PROGRAMS = test-dbus-applet

AM_CPPFLAGS =							\
	$(LIBMATE_PANEL_APPLET_CFLAGS)				\
	-I$(top_builddir)/libmate-panel-applet			\
	-DMATELOCALEDIR=\""$(datadir)/locale"\"	\
	$(DISABLE_DEPRECATED_CFLAGS)

AM_CFLAGS = $(WARN_CFLAGS)

libmate_panel_appletincludedir = $(includedir)/mate-panel-4.0/libmate-panel-applet
libmate_panel_appletinclude_HEADERS =	\
	mate-panel-applet.h			\
	mate-panel-applet-gsettings.h		\
	mate-panel-applet-enums.h

libmate_panel_applet_4_la_SOURCES =		\
	$(BUILT_SOURCES)		\
	mate-panel-applet.h			\
	mate-panel-applet.c			\
	mate-panel-applet-factory.h		\
	mate-panel-applet-factory.c		\
	mate-panel-applet-gsettings.c		\
	mate-panel-applet-gsettings.h       \
	panel-applet-private.h

if ENABLE_X11
libmate_panel_applet_4_la_SOURCES +=	\
	panel-plug.c			\
	panel-plug-private.h
endif

libmate_panel_applet_4_la_LIBADD  = \
	$(LIBMATE_PANEL_APPLET_LIBS) \
	$(X_LIBS)

libmate_panel_applet_4_la_LDFLAGS = \
	-version-info $(LIB_MATE_PANEL_APPLET_LT_VERSION)	\
	-no-undefined

test_dbus_applet_LDADD =	\
	$(LIBMATE_PANEL_APPLET_LIBS)	\
	libmate-panel-applet-4.la

$(libmate_panel_applet_4_la_OBJECTS) $(test_dbus_applet_OBJECTS): $(BUILT_SOURCES)

mate-panel-applet-marshal.h: mate-panel-applet-marshal.list $(GLIB_GENMARSHAL)
	$(AM_V_GEN)$(GLIB_GENMARSHAL) $< --header --prefix=mate_panel_applet_marshal > $@

mate-panel-applet-marshal.c: mate-panel-applet-marshal.list $(GLIB_GENMARSHAL)
	$(AM_V_GEN)echo "#include \"mate-panel-applet-marshal.h\"" > $@ && \
	$(GLIB_GENMARSHAL) $< --body --prefix=mate_panel_applet_marshal >> $@

mate_panel_applet_enum_headers = $(top_srcdir)/libmate-panel-applet/mate-panel-applet.h

mate-panel-applet-enums.c: @REBUILD@ $(mate_panel_applet_enum_headers)
	$(AM_V_GEN)glib-mkenums \
			--fhead "#include <glib-object.h>\n" \
			--fhead "#include \"mate-panel-applet-enums.h\"\n\n" \
			--fprod "\n/* enumerations from \"@filename@\" */" \
			--fprod "\n#include \"@filename@\"\n" \
			--vhead "static const G@Type@Value _@enum_name@_values[] = {" \
			--vprod "  { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" }," \
			--vtail "  { 0, NULL, NULL }\n};\n\n" \
			--vtail "GType\n@enum_name@_get_type (void)\n{\n" \
			--vtail "  static GType type = 0;\n\n" \
			--vtail "  if (!type)\n" \
			--vtail "    type = g_@type@_register_static (\"@EnumName@\", _@enum_name@_values);\n\n" \
			--vtail "  return type;\n}\n\n" \
		$(mate_panel_applet_enum_headers) > $@

mate-panel-applet-enums.h: @REBUILD@ $(mate_panel_applet_enum_headers)
	$(AM_V_GEN)glib-mkenums \
			--fhead "#ifndef __MATE_PANEL_APPLET_ENUMS_H__\n" \
			--fhead "#define __MATE_PANEL_APPLET_ENUMS_H__\n\n" \
			--fhead "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n" \
			--ftail "#ifdef __cplusplus\n}\n#endif\n\n" \
			--ftail "#endif /* __MATE_PANEL_APPLET_ENUMS_H__ */\n" \
			--fprod "\n/* --- @filename@ --- */" \
			--eprod "#define PANEL_TYPE_@ENUMNAME@ @enum_name@_get_type()\n" \
			--eprod "GType @enum_name@_get_type (void);\n" \
		$(mate_panel_applet_enum_headers) > $@

BUILT_SOURCES = \
	mate-panel-applet-enums.c			\
	mate-panel-applet-enums.h			\
	mate-panel-applet-marshal.c			\
	mate-panel-applet-marshal.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libmatepanelapplet-4.0.pc

appletdir       = $(datadir)/mate-panel/applets
applet_in_files = org.mate.panel.TestApplet.mate-panel-applet.desktop.in
noinst_DATA     = $(applet_in_files:.mate-panel-applet.desktop.in=.mate-panel-applet)
$(noinst_DATA): $(applet_in_files)
	$(AM_V_GEN) $(MSGFMT) --desktop --keyword Name --keyword Description --template $< -d $(top_srcdir)/po -o $@

EXTRA_DIST =						\
	org.mate.panel.TestApplet.mate-panel-applet.desktop.in	\
	mate-panel-applet-marshal.list			\
	libmatepanelapplet-4.0.pc.in			\
	libmatepanelapplet-4.0-uninstalled.pc.in

CLEANFILES = $(BUILT_SOURCES) $(noinst_DATA)

###############################
# Introspection generation

-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = --warn-all --add-include-path=$(srcdir) --add-include-path=${prefix}/share/gir-1.0
INTROSPECTION_COMPILER_ARGS = --includedir=$(srcdir)

if HAVE_INTROSPECTION
introspection_sources = 	\
	mate-panel-applet.c		\
	mate-panel-applet.h		\
	mate-panel-applet-gsettings.c	\
	mate-panel-applet-gsettings.h
built_introspection_sources =	\
	mate-panel-applet-enums.c	\
	mate-panel-applet-enums.h

MatePanelApplet-4.0.gir: libmate-panel-applet-4.la
MatePanelApplet_4_0_gir_INCLUDES = GObject-2.0 Gtk-3.0 Gio-2.0
MatePanelApplet_4_0_gir_CFLAGS = $(AM_CPPFLAGS) -I$(srcdir)
MatePanelApplet_4_0_gir_LIBS = libmate-panel-applet-4.la
MatePanelApplet_4_0_gir_FILES = $(addprefix $(srcdir)/,$(introspection_sources)) $(addprefix $(builddir)/,$(built_introspection_sources))
# When requiring gobject-introspection 0.9.5, use this:
#MatePanelApplet_4_0_gir_SCANNERFLAGS = --identifier-prefix=Panel --symbol-prefix=panel --pkg-export=libmatepanelapplet-4.0
MatePanelApplet_4_0_gir_SCANNERFLAGS = --strip-prefix=MatePanel --pkg-export=libmatepanelapplet-4.0
INTROSPECTION_GIRS += MatePanelApplet-4.0.gir

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(gir_DATA) $(typelib_DATA)
endif

################################

dist-hook:
	cd $(distdir) ; rm -f $(CLEANFILES)

-include $(top_srcdir)/git.mk
