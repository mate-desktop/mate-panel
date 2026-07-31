doc_module = 'mate-panel-applet'

gtkdoc_link_dep = declare_dependency(
  link_with: libmate_panel_applet_lib,
  include_directories: libmate_panel_applet_inc,
)

gnome.gtkdoc(doc_module,
  main_sgml: '@0@-docs.sgml'.format(doc_module),
  src_dir: 'libmate-panel-applet',
  gobject_typesfile: '@0@.types'.format(doc_module),
  ignore_headers: [
    'MATE_Panel.h',
    'mate-panel-applet-enums.h',
    'mate-panel-applet-marshal.h',
    'mate-panel-applet-private.h',
    'mate-panel-applet-shell.h',
  ],
  mkdb_args: '--output-format=xml',
  dependencies: libmate_panel_applet_deps + [gtkdoc_link_dep],
  install: true,
)
