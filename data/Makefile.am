SUBDIRS = \
	theme

layoutdir   = $(datadir)/mate-panel/layouts
layout_DATA = \
	default.layout \
	fedora.layout \
	linuxmint.layout \
	mageia.layout \
	opensuse.layout \
	ubuntu.layout

gsettings_ENUM_NAMESPACE = org.mate.panel
gsettings_ENUM_FILES = $(top_srcdir)/mate-panel/panel-enums-gsettings.h

panel_gschemas_in = \
	org.mate.panel.gschema.xml.in \
	org.mate.panel.object.gschema.xml.in \
	org.mate.panel.toplevel.gschema.xml.in \
	org.mate.panel.menubar.gschema.xml.in

gsettings_SCHEMAS = $(panel_gschemas_in:.xml.in=.xml)

@GSETTINGS_RULES@

EXTRA_DIST = \
	$(panel_gschemas_in) \
	$(layout_DATA)

CLEANFILES = $(gsettings_SCHEMAS)
