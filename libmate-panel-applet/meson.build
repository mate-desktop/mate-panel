libmate_panel_applet_inc = include_directories('.')

glib_mkenums = find_program('glib-mkenums')

# generated marshallers
libmate_panel_applet_marshal = gnome.genmarshal('mate-panel-applet-marshal',
  sources: 'mate-panel-applet-marshal.list',
  prefix: 'mate_panel_applet_marshal',
)

# generated enumerations
mate_panel_applet_enum_headers = files('mate-panel-applet.h')

libmate_panel_applet_enums_h = custom_target('mate-panel-applet-enums.h',
  input: mate_panel_applet_enum_headers,
  output: 'mate-panel-applet-enums.h',
  install: true,
  install_dir: join_paths(get_option('includedir'),
    'mate-panel-@0@/libmate-panel-applet'.format(mate_panel_api_version)),
  command: [
    glib_mkenums,
    '--fhead', '#ifndef __MATE_PANEL_APPLET_ENUMS_H__\n#define __MATE_PANEL_APPLET_ENUMS_H__\n\n#ifdef __cplusplus\nextern "C" {\n#endif\n\n',
    '--ftail', '#ifdef __cplusplus\n}\n#endif\n\n#endif /* __MATE_PANEL_APPLET_ENUMS_H__ */\n',
    '--fprod', '\n/* --- @filename@ --- */',
    '--eprod', '#define PANEL_TYPE_@ENUMNAME@ @enum_name@_get_type()\nGType @enum_name@_get_type (void);\n',
    '--output', '@OUTPUT@',
    '@INPUT@',
  ],
)

libmate_panel_applet_enums_c = custom_target('mate-panel-applet-enums.c',
  input: mate_panel_applet_enum_headers,
  output: 'mate-panel-applet-enums.c',
  command: [
    glib_mkenums,
    '--fhead', '#include <glib-object.h>\n#include "mate-panel-applet-enums.h"\n',
    '--fprod', '\n/* enumerations from "@filename@" */\n#include "@filename@"\n',
    '--vhead', 'static const G@Type@Value _@enum_name@_values[] = {',
    '--vprod', '  { @VALUENAME@, "@VALUENAME@", "@valuenick@" },',
    '--vtail', '  { 0, NULL, NULL }\n};\n\n',
    '--vtail', 'GType\n@enum_name@_get_type (void)\n{\n',
    '--vtail', '  static GType type = 0;\n\n',
    '--vtail', '  if (!type)\n',
    '--vtail', '    type = g_@type@_register_static ("@EnumName@", _@enum_name@_values);\n\n',
    '--vtail', '  return type;\n}\n\n',
    '--output', '@OUTPUT@',
    '@INPUT@',
  ],
)

libmate_panel_applet_sources = [
  'mate-panel-applet.h',
  'mate-panel-applet.c',
  'mate-panel-applet-factory.h',
  'mate-panel-applet-factory.c',
  'mate-panel-applet-gsettings.c',
  'mate-panel-applet-gsettings.h',
  'panel-applet-private.h',
  libmate_panel_applet_marshal,
  libmate_panel_applet_enums_c,
  libmate_panel_applet_enums_h,
]

if have_x11
  libmate_panel_applet_sources += ['panel-plug.c', 'panel-plug-private.h']
endif

libmate_panel_applet_deps_local = libmate_panel_applet_deps
if have_x11
  libmate_panel_applet_deps_local += x11_dep
endif

libmate_panel_applet_lib = library('mate-panel-applet-4',
  libmate_panel_applet_sources,
  version: '@0@.@1@.@2@'.format(libmate_panel_applet_current, libmate_panel_applet_age, libmate_panel_applet_revision),
  include_directories: libmate_panel_applet_inc,
  dependencies: libmate_panel_applet_deps_local,
  c_args: disable_deprecated_flags,
  install: true,
)

install_headers(
  'mate-panel-applet.h',
  'mate-panel-applet-gsettings.h',
  subdir: 'mate-panel-@0@/libmate-panel-applet'.format(mate_panel_api_version),
)

pkg = import('pkgconfig')

pkg.generate(libmate_panel_applet_lib,
  filebase: 'libmatepanelapplet-@0@'.format(mate_panel_api_version),
  name: 'libmate-panel-applet-4',
  description: 'libmate-panel-applet-4',
  version: mate_panel_version,
  requires: ['glib-2.0', 'gio-2.0', 'gtk+-3.0'],
  subdirs: 'mate-panel-@0@/libmate-panel-applet'.format(mate_panel_api_version),
  install_dir: join_paths(mate_panel_libdir, 'pkgconfig'),
)

# test program
executable('test-dbus-applet',
  'test-dbus-applet.c',
  include_directories: libmate_panel_applet_inc,
  dependencies: libmate_panel_applet_deps_local,
  link_with: libmate_panel_applet_lib,
)

# test applet desktop file (not installed)
test_applet_desktop = i18n.merge_file(
  input: 'org.mate.panel.TestApplet.mate-panel-applet.desktop.in',
  output: 'org.mate.panel.TestApplet.mate-panel-applet',
  type: 'desktop',
  po_dir: '../po',
  install: false,
)

# introspection
if enable_introspection.allowed()
  introspection_sources = [
    'mate-panel-applet.c',
    'mate-panel-applet.h',
    'mate-panel-applet-gsettings.c',
    'mate-panel-applet-gsettings.h',
  ]
  built_introspection_sources = [
    libmate_panel_applet_enums_c,
    libmate_panel_applet_enums_h,
  ]

  mate_panel_applet_gir = gnome.generate_gir(libmate_panel_applet_lib,
    sources: introspection_sources + built_introspection_sources,
    nsversion: mate_panel_gir_version,
    namespace: mate_panel_gir_ns,
    symbol_prefix: 'mate_panel_applet',
    identifier_prefix: 'MatePanelApplet',
    export_packages: 'libmatepanelapplet-@0@'.format(mate_panel_api_version),
    includes: ['GObject-2.0', 'Gtk-3.0', 'Gio-2.0'],
    install: true,
    extra_args: [
      '--warn-all',
      '--strip-prefix=MatePanel',
      '--add-include-path=@0@'.format(meson.current_source_dir()),
    ],
  )
endif
