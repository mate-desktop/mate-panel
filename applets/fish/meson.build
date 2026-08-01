mate_panel_fishdir = join_paths(mate_panel_pkgdatadir, 'fish')

fish_resource_deps = files(
  'fish.ui',
  'fish-menu.xml',
)

fish_resources_c = custom_target('fish-resources.c',
  input: 'fish.gresource.xml',
  output: 'fish-resources.c',
  depend_files: fish_resource_deps,
  command: [
    glib_compile_resources,
    '--target', '@OUTPUT@',
    '--sourcedir', meson.current_source_dir(),
    '--generate-source',
    '--c-name', 'fish',
    '@INPUT@',
  ],
)

fish_resources_h = custom_target('fish-resources.h',
  input: 'fish.gresource.xml',
  output: 'fish-resources.h',
  depend_files: fish_resource_deps,
  command: [
    glib_compile_resources,
    '--target', '@OUTPUT@',
    '--sourcedir', meson.current_source_dir(),
    '--generate-header',
    '--c-name', 'fish',
    '@INPUT@',
  ],
)

fish_built_sources = [
  fish_resources_c,
  fish_resources_h,
]

fish_sources = [
  'fish.c',
  fish_built_sources,
]

fish_deps = [
  gtk_dep,
  cairo_dep,
  mate_desktop_dep,
] + libmate_panel_applet_deps

fish_inc_dirs = [
  include_directories('.'),
  libmate_panel_applet_inc,
]

fish_c_args = [
  '-DMATELOCALEDIR="@0@"'.format(mate_panel_localedir),
  '-DFISH_ICONDIR="@0@"'.format(mate_panel_fishdir),
] + disable_deprecated_flags

if fish_inprocess
  fish_applet = shared_library('fish-applet',
    fish_sources,
    include_directories: fish_inc_dirs,
    dependencies: fish_deps,
    c_args: fish_c_args,
    link_with: libmate_panel_applet_lib,
    install: true,
    install_dir: mate_panel_pkglibdir,
  )
  fish_applet_in_process = 'true'
  fish_applet_location = join_paths(mate_panel_pkglibdir, 'libfish-applet.so')
else
  fish_applet = executable('fish-applet',
    fish_sources,
    include_directories: fish_inc_dirs,
    dependencies: fish_deps,
    c_args: fish_c_args,
    link_with: libmate_panel_applet_lib,
    install: true,
    install_dir: mate_panel_libexecdir,
  )
  fish_applet_in_process = 'false'
  fish_applet_location = join_paths(mate_panel_libexecdir, 'fish-applet')
endif

# applet registration file
fish_applet_desktop_in = configure_file(
  input: 'org.mate.panel.FishApplet.mate-panel-applet.desktop.in.in',
  output: 'org.mate.panel.FishApplet.mate-panel-applet.desktop.in',
  configuration: {
    'IN_PROCESS': fish_applet_in_process,
    'LOCATION': fish_applet_location,
    'VERSION': mate_panel_version,
  },
)

fish_applet_desktop = i18n.merge_file(
  input: fish_applet_desktop_in,
  output: 'org.mate.panel.FishApplet.mate-panel-applet',
  type: 'desktop',
  po_dir: '../../po',
  install: true,
  install_dir: mate_panel_appletsdir,
)

if not fish_inprocess
  fish_service = configure_file(
    input: 'org.mate.panel.applet.FishAppletFactory.service.in',
    output: 'org.mate.panel.applet.FishAppletFactory.service',
    configuration: {
      'LOCATION': fish_applet_location,
    },
  )
  install_data(fish_service,
    install_dir: join_paths(mate_panel_datadir, 'dbus-1', 'services'),
  )
endif

# fish bitmaps
install_data(
  'wanda.png',
  'fishanim.png',
  'oldwanda.png',
  'footguy.png',
  'monkey.png',
  install_dir: mate_panel_fishdir,
)

# gsettings schema
fish_gschema = configure_file(
  input: 'org.mate.panel.applet.fish.gschema.xml.in',
  output: 'org.mate.panel.applet.fish.gschema.xml',
  configuration: {'GETTEXT_PACKAGE': mate_panel_gettext_package},
)

install_data(fish_gschema, install_dir: mate_panel_schemadir)
