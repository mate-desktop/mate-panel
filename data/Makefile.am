layoutdir   = $(datadir)/mate-panel/layouts
layout_DATA = \
	default.layout \
	fedora.layout \
	linuxmint.layout \
	opensuse.layout \
	ubuntu.layout

@INTLTOOL_XML_NOMERGE_RULE@

gsettings_ENUM_NAMESPACE = org.mate.panel
gsettings_ENUM_FILES = $(top_srcdir)/mate-panel/panel-enums-gsettings.h

panel_gschemas_in_in = \
	org.mate.panel.gschema.xml.in.in \
	org.mate.panel.object.gschema.xml.in.in \
	org.mate.panel.toplevel.gschema.xml.in.in \
	org.mate.panel.menubar.gschema.xml.in.in

panel_gschemas_in = $(panel_gschemas_in_in:.xml.in.in=.xml.in)
gsettings_SCHEMAS = $(panel_gschemas_in:.xml.in=.xml)

%.gschema.xml.in: %.gschema.xml.in.in Makefile
	$(AM_V_GEN) $(SED) -e 's^\@GETTEXT_PACKAGE\@^$(GETTEXT_PACKAGE)^g' < $< > $@

@GSETTINGS_RULES@

convertdir = $(datadir)/MateConf/gsettings
convert_DATA = mate-panel.convert

EXTRA_DIST = \
	$(convert_DATA) \
	$(panel_gschemas_in_in) \
	$(layout_DATA)

CLEANFILES = \
	$(panel_gschemas_in) \
	$(gsettings_SCHEMAS)
