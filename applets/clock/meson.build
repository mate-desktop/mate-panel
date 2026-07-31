clock_enum_headers = ['clock-utils.h']

clock_typebuiltins_h = custom_target('clock-typebuiltins.h',
  input: clock_enum_headers,
  output: 'clock-typebuiltins.h',
  command: [
    glib_mkenums,
    '--fhead', '#ifndef __CLOCK_TYPEBUILTINS_H__\n',
    '--fhead', '#define __CLOCK_TYPEBUILTINS_H__ 1\n\n',
    '--fhead', '#ifdef __cplusplus\nextern "C" {\n#endif\n\n',
    '--ftail', '#ifdef __cplusplus\n}\n#endif\n\n',
    '--ftail', '#endif /* __CLOCK_TYPEBUILTINS_H__ */\n',
    '--fprod', '\n/* --- @filename@ --- */',
    '--eprod', '#define CLOCK_TYPE_@ENUMSHORT@ @enum_name@_get_type()\n',
    '--eprod', 'GType @enum_name@_get_type (void);\n',
    '--output', '@OUTPUT@',
    '@INPUT@',
  ],
)

clock_typebuiltins_c = custom_target('clock-typebuiltins.c',
  input: clock_enum_headers,
  output: 'clock-typebuiltins.c',
  command: [
    glib_mkenums,
    '--fhead', '#include <glib-object.h>\n',
    '--fhead', '#include "clock-typebuiltins.h"\n\n',
    '--fprod', '\n/* enumerations from "@filename@" */',
    '--fprod', '\n#include "@filename@"\n',
    '--vhead', 'static const GEnumValue _@enum_name@_values[] = {',
    '--vprod', '  { @VALUENAME@, "@VALUENAME@", "@valuenick@" },',
    '--vtail', '  { 0, NULL, NULL }\n};\n\n',
    '--vtail', 'GType\n@enum_name@_get_type (void)\n{\n',
    '--vtail', '  static GType type = 0;\n\n',
    '--vtail', '  if (!type)\n',
    '--vtail', '    type = g_enum_register_static ("@EnumName@", _@enum_name@_values);\n\n',
    '--vtail', '  return type;\n}\n\n',
    '--output', '@OUTPUT@',
    '@INPUT@',
  ],
)

clock_marshal = gnome.genmarshal('clock-marshallers',
  sources: 'clock-marshallers.list',
  prefix: '_clock_marshal',
  internal: true,
)

clock_resource_deps = files(
  'clock.ui',
  'clock-menu.xml',
  'pixmaps/clock-face-large.svg',
  'pixmaps/clock-face-small.svg',
  'pixmaps/clock-face-small-morning.svg',
  'pixmaps/clock-face-small-day.svg',
  'pixmaps/clock-face-small-evening.svg',
  'pixmaps/clock-face-small-night.svg',
  'pixmaps/clock-map.png',
  'pixmaps/clock-map-location-marker.png',
  'pixmaps/clock-map-location-current.png',
  'pixmaps/clock-map-location-hilight.png',
)

clock_resources_c = custom_target('clock-resources.c',
  input: 'clock.gresource.xml',
  output: 'clock-resources.c',
  depend_files: clock_resource_deps,
  command: [
    glib_compile_resources,
    '--target', '@OUTPUT@',
    '--sourcedir', meson.current_source_dir(),
    '--generate-source',
    '--c-name', 'clock',
    '@INPUT@',
  ],
)

clock_resources_h = custom_target('clock-resources.h',
  input: 'clock.gresource.xml',
  output: 'clock-resources.h',
  depend_files: clock_resource_deps,
  command: [
    glib_compile_resources,
    '--target', '@OUTPUT@',
    '--sourcedir', meson.current_source_dir(),
    '--generate-header',
    '--c-name', 'clock',
    '@INPUT@',
  ],
)

clock_built_sources = [
  clock_marshal,
  clock_resources_c,
  clock_resources_h,
  clock_typebuiltins_c,
  clock_typebuiltins_h,
]

# system timezone helper
libsystem_timezone = static_library('system-timezone',
  'system-timezone.c',
  'system-timezone.h',
  dependencies: [gio_dep],
  c_args: disable_deprecated_flags,
)

executable('test-system-timezone',
  'test-system-timezone.c',
  dependencies: [gio_dep],
  link_with: libsystem_timezone,
  c_args: disable_deprecated_flags,
)

clock_sources = [
  'calendar-window.c',
  'calendar-window.h',
  'clock.c',
  'clock.h',
  'clock-face.c',
  'clock-face.h',
  'clock-location.c',
  'clock-location.h',
  'clock-location-tile.c',
  'clock-location-tile.h',
  'clock-map.c',
  'clock-map.h',
  'clock-sunpos.c',
  'clock-sunpos.h',
  'clock-utils.c',
  'clock-utils.h',
  'set-timezone.c',
  'set-timezone.h',
  clock_built_sources,
]

if have_eds
  clock_sources += [
    'calendar-client.c',
    'calendar-client.h',
    'calendar-sources.c',
    'calendar-sources.h',
    'calendar-debug.h',
  ]
endif

clock_deps = [
  pango_dep,
  gtk_dep,
  glib_dep,
  gio_dep,
  mateweather_dep,
  mate_desktop_dep,
  m_dep,
] + libmate_panel_applet_deps

if have_eds
  clock_deps += eds_deps
endif

if have_wayland
  clock_deps += wayland_deps
endif

clock_inc_dirs = [
  include_directories('.'),
  libmate_panel_applet_inc,
]

clock_c_args = [
  '-DMATELOCALEDIR="@0@"'.format(mate_panel_localedir),
  '-DMATEWEATHER_I_KNOW_THIS_IS_UNSTABLE',
] + disable_deprecated_flags

if clock_inprocess
  clock_applet = shared_library('clock-applet',
    clock_sources,
    include_directories: clock_inc_dirs,
    dependencies: clock_deps,
    c_args: clock_c_args,
    link_with: [libmate_panel_applet_lib, libsystem_timezone],
    install: true,
    install_dir: mate_panel_pkglibdir,
  )
  clock_applet_in_process = 'true'
  clock_applet_location = join_paths(mate_panel_pkglibdir, 'libclock-applet.so')
else
  clock_applet = executable('clock-applet',
    clock_sources,
    include_directories: clock_inc_dirs,
    dependencies: clock_deps,
    c_args: clock_c_args,
    link_with: [libmate_panel_applet_lib, libsystem_timezone],
    install: true,
    install_dir: mate_panel_libexecdir,
  )
  clock_applet_in_process = 'false'
  clock_applet_location = join_paths(mate_panel_libexecdir, 'clock-applet')
endif

# applet registration file
clock_applet_desktop_in = configure_file(
  input: 'org.mate.panel.ClockApplet.mate-panel-applet.desktop.in.in',
  output: 'org.mate.panel.ClockApplet.mate-panel-applet.desktop.in',
  configuration: {
    'IN_PROCESS': clock_applet_in_process,
    'LOCATION': clock_applet_location,
    'VERSION': mate_panel_version,
  },
)

clock_applet_desktop = i18n.merge_file(
  input: clock_applet_desktop_in,
  output: 'org.mate.panel.ClockApplet.mate-panel-applet',
  type: 'desktop',
  po_dir: '../../po',
  install: true,
  install_dir: mate_panel_appletsdir,
)

if not clock_inprocess
  clock_service = configure_file(
    input: 'org.mate.panel.applet.ClockAppletFactory.service.in',
    output: 'org.mate.panel.applet.ClockAppletFactory.service',
    configuration: {
      'LOCATION': clock_applet_location,
    },
  )
  install_data(clock_service,
    install_dir: join_paths(mate_panel_datadir, 'dbus-1', 'services'),
  )
endif

# gsettings schema
clock_gschema = configure_file(
  input: 'org.mate.panel.applet.clock.gschema.xml.in',
  output: 'org.mate.panel.applet.clock.gschema.xml',
  configuration: {'GETTEXT_PACKAGE': mate_panel_gettext_package},
)

install_data(clock_gschema, install_dir: mate_panel_schemadir)
