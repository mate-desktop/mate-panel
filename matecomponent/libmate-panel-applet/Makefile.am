lib_LTLIBRARIES = libmate-panel-applet-2.la
bin_PROGRAMS = panel-test-applets-matecomponent
noinst_PROGRAMS = test-matecomponent-applet

AM_CPPFLAGS =							\
	$(LIBMATE_PANEL_APPLET_MATECOMPONENT_CFLAGS)			\
	-I$(top_builddir)/matecomponent/libmate-panel-applet		\
	-DMATELOCALEDIR=\""$(prefix)/$(DATADIRNAME)/locale"\"	\
	-DMATE_PANEL_APPLET_DATADIR=\""$(datadir)"\"			\
	-DMATE_PANEL_APPLET_BUILDERDIR=\""$(uidir)"\"		\
	$(DISABLE_DEPRECATED_CFLAGS)

AM_CFLAGS = $(WARN_CFLAGS)

libmate_panel_appletincludedir = $(includedir)/panel-2.0
libmate_panel_appletinclude_HEADERS =	\
	mate-panel-applet.h			\
	mate-panel-applet-mateconf.h		\
	mate-panel-applet-enums.h		\
	MATE_Panel.h

libmate_panel_applet_2_la_SOURCES =		\
	mate-panel-applet.h			\
	mate-panel-applet.c			\
	mate-panel-applet-mateconf.c		\
	mate-panel-applet-mateconf.h		\
	mate-panel-applet-shell.c		\
	mate-panel-applet-shell.h		\
	mate-panel-applet-private.h		\
	mate-panel-applet-marshal.c		\
	mate-panel-applet-marshal.h		\
	mate-panel-applet-enums.c		\
	mate-panel-applet-enums.h		\
	$(CORBA_SRCLIST)

libmate_panel_applet_2_la_LDFLAGS = \
	-version-info $(LIB_MATE_PANEL_APPLET_MATECOMPONENT_LT_VERSION) \
	-no-undefined
libmate_panel_applet_2_la_LIBADD  = \
	$(LIBMATE_PANEL_APPLET_MATECOMPONENT_LIBS) \
	$(X_LIBS)

panel_test_applets_matecomponent_LDADD   =	\
	$(LIBMATE_PANEL_APPLET_MATECOMPONENT_LIBS)

test_matecomponent_applet_LDADD =		\
	$(LIBMATE_PANEL_APPLET_MATECOMPONENT_LIBS)	\
	libmate-panel-applet-2.la

$(libmate_panel_applet_2_la_OBJECTS) $(test_matecomponent_applet_matecomponent_OBJECTS): $(BUILT_SOURCES)

CORBA_SRCLIST =			\
	MATE_Panel-stubs.c	\
	MATE_Panel-skels.c	\
	MATE_Panel-common.c	\
	MATE_Panel.h

$(CORBA_SRCLIST): $(top_srcdir)/matecomponent/idl/MATE_Panel.idl $(MATECORBA_IDL)
	$(AM_V_GEN)$(MATECORBA_IDL) -I $(MATECOMPONENT_IDLDIR) -I $(MATECOMPONENT_ACT_IDLDIR) $(top_srcdir)/matecomponent/idl/MATE_Panel.idl

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
	$(CORBA_SRCLIST)	\
	mate-panel-applet-enums.c	\
	mate-panel-applet-enums.h	\
	mate-panel-applet-marshal.c	\
	mate-panel-applet-marshal.h

uidir   = $(datadir)/mate-panel/ui
ui_DATA = panel-test-applets-matecomponent.ui

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libmatepanelapplet-2.0.pc

serverdir       = $(libdir)/matecomponent/servers
server_in_files = MATE_Panel_TestApplet.server.in
noinst_DATA     = $(server_in_files:.server.in=.server)

@INTLTOOL_SERVER_RULE@

EXTRA_DIST =					\
	MATE_Panel_TestApplet.server.in	\
	mate-panel-applet-marshal.list		\
	libmatepanelapplet-2.0.pc.in		\
	libmatepanelapplet-2.0-uninstalled.pc.in	\
	panel-test-applets-matecomponent.ui

CLEANFILES = $(BUILT_SOURCES) $(noinst_DATA)

dist-hook:
	cd $(distdir) ; rm -f $(CLEANFILES)

-include $(top_srcdir)/git.mk
