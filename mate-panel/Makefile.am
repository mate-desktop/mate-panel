SUBDIRS = libegg libmate-panel-applet-private libpanel-util

bin_PROGRAMS = \
	mate-panel 			\
	mate-desktop-item-edit		\
	mate-panel-test-applets
libexec_SCRIPTS = mate-panel-add

AM_CPPFLAGS =							\
	$(PANEL_CFLAGS)						\
	-I.							\
	-I$(srcdir)						\
	-I$(top_builddir)/mate-panel				\
	-I$(top_builddir)/mate-panel/libpanel-util		\
	-DMATELOCALEDIR=\""$(prefix)/$(DATADIRNAME)/locale"\"	\
	-DBUILDERDIR=\""$(uidir)"\"				\
	-DICONDIR=\""$(datadir)/mate-panel/pixmaps"\"		\
	$(DISABLE_DEPRECATED_CFLAGS)

AM_CFLAGS = $(WARN_CFLAGS)

panel_sources =			\
	panel-typebuiltins.c	\
	panel-typebuiltins.h	\
	panel-marshal.c		\
	panel-marshal.h		\
	main.c			\
	panel-widget.c		\
	button-widget.c		\
	xstuff.c		\
	panel-session.c		\
	panel-compatibility.c	\
	panel.c			\
	applet.c		\
	drawer.c		\
	panel-config-global.c	\
	panel-util.c		\
	panel-mateconf.c		\
	panel-properties-dialog.c	\
	panel-run-dialog.c	\
	menu.c			\
	panel-context-menu.c	\
	launcher.c		\
	mate-panel-applet-frame.c	\
	mate-panel-applets-manager.c	\
	panel-shell.c		\
	panel-background.c	\
	panel-background-monitor.c	\
	panel-stock-icons.c	\
	panel-action-button.c	\
	panel-menu-bar.c	\
	panel-menu-button.c	\
	panel-menu-items.c	\
	panel-separator.c	\
	panel-recent.c		\
	panel-action-protocol.c	\
	panel-toplevel.c	\
	panel-struts.c		\
	panel-frame.c		\
	panel-xutils.c		\
	panel-multiscreen.c	\
	panel-a11y.c		\
	panel-bindings.c	\
	panel-profile.c		\
	panel-force-quit.c	\
	panel-lockdown.c	\
	panel-addto.c		\
	panel-ditem-editor.c	\
	panel-modules.c		\
	mate-panel-applet-info.c \
	applet-signaler.c	\
	panel-reset.c

panel_headers =			\
	panel-types.h		\
	panel-widget.h		\
	panel-globals.h		\
	button-widget.h		\
	xstuff.h		\
	panel-session.h		\
	panel-compatibility.h	\
	panel.h			\
	applet.h		\
	drawer.h		\
	panel-util.h		\
	panel-properties-dialog.h	\
	panel-config-global.h	\
	panel-mateconf.h		\
	panel-run-dialog.h	\
	menu.h			\
	panel-context-menu.h	\
	launcher.h		\
	mate-panel-applet-frame.h	\
	mate-panel-applets-manager.h	\
	panel-shell.h		\
	panel-background.h	\
	panel-background-monitor.h	\
	panel-stock-icons.h	\
	panel-action-button.h	\
	panel-menu-bar.h	\
	panel-menu-button.h	\
	panel-menu-items.h	\
	panel-separator.h	\
	panel-recent.h		\
	panel-action-protocol.h	\
	panel-toplevel.h	\
	panel-struts.h		\
	panel-frame.h		\
	panel-xutils.h		\
	panel-multiscreen.h	\
	panel-a11y.h		\
	panel-bindings.h	\
	panel-profile.h		\
	panel-enums.h		\
	panel-force-quit.h	\
	panel-lockdown.h	\
	panel-addto.h		\
	panel-ditem-editor.h	\
	panel-icon-names.h	\
	panel-modules.h		\
	mate-panel-applet-info.h \
	applet-signaler.h	\
	panel-reset.h

mate_panel_SOURCES =			\
	$(panel_sources)		\
	$(panel_headers)

mate_panel_CPPFLAGS = 				\
	$(AM_CPPFLAGS)				\
	$(XRANDR_CFLAGS)			\
	-DPANEL_MODULES_DIR=\"$(modulesdir)\"	\
	-DMATEMENU_I_KNOW_THIS_IS_UNSTABLE

mate_panel_LDADD =		\
	$(top_builddir)/mate-panel/libegg/libegg.la					\
	$(top_builddir)/mate-panel/libmate-panel-applet-private/libmate-panel-applet-private.la	\
	$(top_builddir)/mate-panel/libpanel-util/libpanel-util.la			\
	$(PANEL_LIBS)									\
	$(XRANDR_LIBS)									\
	$(X_LIBS) \
	-lm

mate_panel_LDFLAGS = -export-dynamic

mate_desktop_item_edit_SOURCES =	\
	mate-desktop-item-edit.c	\
	panel-ditem-editor.c		\
	panel-marshal.c			\
	panel-util.c			\
	xstuff.c

mate_desktop_item_edit_LDADD = 	\
	$(top_builddir)/mate-panel/libpanel-util/libpanel-util.la \
	$(PANEL_LIBS) \
	-lX11

mate_panel_test_applets_SOURCES =		\
	panel-modules.c			\
	mate-panel-applet-info.c		\
	mate-panel-applets-manager.c		\
	panel-marshal.c			\
	panel-test-applets.c

mate_panel_test_applets_CPPFLAGS =			\
	$(AM_CPPFLAGS)				\
	-DPANEL_MODULES_DIR=\"$(modulesdir)\"	\
	-DMATE_PANEL_APPLETS_DIR=\"$(appletsdir)\"

mate_panel_test_applets_LDADD = 		\
	$(top_builddir)/mate-panel/libmate-panel-applet-private/libmate-panel-applet-private-mini.la	\
	$(top_builddir)/mate-panel/libpanel-util/libpanel-util.la				\
	$(PANEL_LIBS)

mate_panel_test_applets_LDFLAGS = -export-dynamic

mate-panel-add: mate-panel-add.in Makefile
	$(AM_V_GEN)sed 						\
		-e s!\@PYTHON\@!@PYTHON@! 			\
		-e s!\@MATE_PANEL_APPLETS_DIR\@!$(appletsdir)! 	\
		< $< > $@
	$(AM_V_at)chmod a+x $@

panel_enum_headers =					\
	$(top_srcdir)/mate-panel/panel-enums.h		\
	$(top_srcdir)/mate-panel/panel-types.h

panel-marshal.h: panel-marshal.list $(GLIB_GENMARSHAL)
	$(AM_V_GEN)$(GLIB_GENMARSHAL) $< --header --prefix=panel_marshal > $@

panel-marshal.c: panel-marshal.list $(GLIB_GENMARSHAL)
	$(AM_V_GEN)echo "#include \"panel-marshal.h\"" > $@ && \
	$(GLIB_GENMARSHAL) $< --body --prefix=panel_marshal >> $@

panel-typebuiltins.c: @REBUILD@ $(panel_enum_headers)
	$(AM_V_GEN)glib-mkenums \
			--fhead "#include <glib-object.h>\n" \
			--fhead "#include \"panel-typebuiltins.h\"\n\n" \
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
		$(panel_enum_headers) > $@

panel-typebuiltins.h: @REBUILD@ $(panel_enum_headers)
	$(AM_V_GEN)glib-mkenums \
			--fhead "#ifndef __PANEL_TYPEBUILTINS_H__\n" \
			--fhead "#define __PANEL_TYPEBUILTINS_H__ 1\n\n" \
			--fhead "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n" \
			--ftail "#ifdef __cplusplus\n}\n#endif\n\n" \
			--ftail "#endif /* __PANEL_TYPEBUILTINS_H__ */\n" \
			--fprod "\n/* --- @filename@ --- */" \
			--eprod "#define PANEL_TYPE_@ENUMSHORT@ @enum_name@_get_type()\n" \
			--eprod "GType @enum_name@_get_type (void);\n" \
		$(panel_enum_headers) > $@

BUILT_SOURCES = \
	panel-typebuiltins.c		\
	panel-typebuiltins.h		\
	panel-marshal.c			\
	panel-marshal.h

rcdir = $(datadir)
rc_DATA = mate-panelrc

uidir = $(datadir)/mate-panel/ui
ui_DATA =					\
	panel-properties-dialog.ui		\
	panel-run-dialog.ui			\
	panel-test-applets.ui

desktopdir = $(datadir)/applications
desktop_in_files = mate-panel.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

@INTLTOOL_DESKTOP_RULE@

entriesdir       = $(MATECONF_SCHEMA_FILE_DIR)
entries_DATA     = panel-default-setup.entries

schemasdir       = $(MATECONF_SCHEMA_FILE_DIR)
schemas_in_files =				\
	panel-global.schemas.in 		\
	panel-general.schemas.in		\
	panel-toplevel.schemas.in		\
	panel-object.schemas.in
schemas_DATA     = panel-compatibility.schemas $(schemas_in_files:.schemas.in=.schemas)

@INTLTOOL_SCHEMAS_RULE@

if MATECONF_SCHEMAS_INSTALL
install-data-local:
	if test -z "$(DESTDIR)" ; then \
		for p in $(schemas_DATA) ; do \
			MATECONF_CONFIG_SOURCE=$(MATECONF_SCHEMA_CONFIG_SOURCE) $(MATECONFTOOL) --makefile-install-rule $(top_builddir)/mate-panel/$$p ; \
		done ; \
		$(MATECONFTOOL) --direct --config-source=$(MATECONF_SCHEMA_CONFIG_SOURCE) --load $(srcdir)/panel-default-setup.entries ; \
		$(MATECONFTOOL) --direct --config-source=$(MATECONF_SCHEMA_CONFIG_SOURCE) --load $(srcdir)/panel-default-setup.entries /apps/panel ; \
	fi
uninstall-local:
	for p in $(schema_DATA) ; do \
		MATECONF_CONFIG_SOURCE=$(MATECONF_SCHEMA_CONFIG_SOURCE) $(MATECONFTOOL) --makefile-uninstall-rule $(top_builddir)/applets/ngome-panel/$$p ; \
	done
endif

EXTRA_DIST =					\
	$(ui_DATA)				\
	nothing.cP				\
	nothing.h				\
	$(schemas_in_files)			\
	panel-compatibility.schemas		\
	panel-marshal.list			\
	mate-panelrc				\
	$(entries_DATA)				\
	$(desktop_in_files)			\
	mate-panel-add.in

CLEANFILES = \
	$(BUILT_SOURCES) \
	$(schemas_in_files:.schemas.in=.schemas) \
	$(sys_DATA) \
	$(desktop_DATA) \
	mate-panel-add

dist-hook:
	cd $(distdir) ; rm -f $(CLEANFILES)

-include $(top_srcdir)/git.mk
