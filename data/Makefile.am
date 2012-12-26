default_layoutdir   = $(datadir)/mate-panel
default_layout_DATA = panel-default-layout.mate

@INTLTOOL_XML_NOMERGE_RULE@

gsettings_ENUM_NAMESPACE = org.mate.panel
gsettings_ENUM_FILES = $(top_srcdir)/mate-panel/panel-enums-gsettings.h

gsettings_SCHEMAS = \
	org.mate.panel.gschema.xml \
	org.mate.panel.object.gschema.xml \
	org.mate.panel.toplevel.gschema.xml

@GSETTINGS_RULES@

convertdir = $(datadir)/MateConf/gsettings
convert_DATA = mate-panel.convert

EXTRA_DIST = $(convert_DATA)
