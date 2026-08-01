wncklet_resource_deps = files(
  'showdesktop-menu.xml',
  'window-list.ui',
  'window-list-menu.xml',
  'window-menu-menu.xml',
  'workspace-switcher.ui',
  'workspace-switcher-menu.xml',
)

wncklet_resources_c = custom_target('wncklet-resources.c',
  input: 'wncklet.gresource.xml',
  output: 'wncklet-resources.c',
  depend_files: wncklet_resource_deps,
  command: [
    glib_compile_resources,
    '--target', '@OUTPUT@',
    '--sourcedir', meson.current_source_dir(),
    '--generate-source',
    '--c-name', 'wncklet',
    '@INPUT@',
  ],
)

wncklet_resources_h = custom_target('wncklet-resources.h',
  input: 'wncklet.gresource.xml',
  output: 'wncklet-resources.h',
  depend_files: wncklet_resource_deps,
  command: [
    glib_compile_resources,
    '--target', '@OUTPUT@',
    '--sourcedir', meson.current_source_dir(),
    '--generate-header',
    '--c-name', 'wncklet',
    '@INPUT@',
  ],
)

wncklet_built_sources = [
  wncklet_resources_c,
  wncklet_resources_h,
]

wncklet_sources = [
  'wncklet.c',
  'wncklet.h',
  'window-menu.c',
  'window-menu.h',
  'window-list.c',
  'window-list.h',
  'showdesktop.c',
  'showdesktop.h',
  'workspace-switcher.c',
  'workspace-switcher.h',
  wncklet_built_sources,
]

if have_wayland
  wncklet_sources += [
    'wayland-backend.c',
    'wayland-backend.h',
    'wayland-workspace.c',
    'wayland-workspace.h',
    'wayland-protocol/wlr-foreign-toplevel-management-unstable-v1-code.c',
    'wayland-protocol/wlr-foreign-toplevel-management-unstable-v1-client.h',
  ]
endif

wncklet_deps = [
  gtk_dep,
  wnck_dep,
  mate_desktop_dep,
] + libmate_panel_applet_deps

if have_x11
  wncklet_deps += x11_dep
endif

if have_wayland
  wncklet_deps += wayland_deps + [xfw_dep]
endif

wncklet_inc_dirs = [
  include_directories('.'),
  libmate_panel_applet_inc,
]

wncklet_c_args = [
  '-DMATELOCALEDIR="@0@"'.format(mate_panel_localedir),
] + disable_deprecated_flags

if wncklet_inprocess
  wncklet_applet = shared_library('wnck-applet',
    wncklet_sources,
    include_directories: wncklet_inc_dirs,
    dependencies: wncklet_deps,
    c_args: wncklet_c_args,
    link_with: libmate_panel_applet_lib,
    install: true,
    install_dir: mate_panel_pkglibdir,
  )
  wncklet_applet_in_process = 'true'
  wncklet_applet_location = join_paths(mate_panel_pkglibdir, 'libwnck-applet.so')
else
  wncklet_applet = executable('wnck-applet',
    wncklet_sources,
    include_directories: wncklet_inc_dirs,
    dependencies: wncklet_deps,
    c_args: wncklet_c_args,
    link_with: libmate_panel_applet_lib,
    install: true,
    install_dir: mate_panel_libexecdir,
  )
  wncklet_applet_in_process = 'false'
  wncklet_applet_location = join_paths(mate_panel_libexecdir, 'wnck-applet')
endif

# applet registration file
wncklet_applet_desktop_in = configure_file(
  input: 'org.mate.panel.Wncklet.mate-panel-applet.desktop.in.in',
  output: 'org.mate.panel.Wncklet.mate-panel-applet.desktop.in',
  configuration: {
    'IN_PROCESS': wncklet_applet_in_process,
    'LOCATION': wncklet_applet_location,
    'VERSION': mate_panel_version,
  },
)

wncklet_applet_desktop = i18n.merge_file(
  input: wncklet_applet_desktop_in,
  output: 'org.mate.panel.Wncklet.mate-panel-applet',
  type: 'desktop',
  po_dir: '../../po',
  install: true,
  install_dir: mate_panel_appletsdir,
)

if not wncklet_inprocess
  wncklet_service = configure_file(
    input: 'org.mate.panel.applet.WnckletFactory.service.in',
    output: 'org.mate.panel.applet.WnckletFactory.service',
    configuration: {
      'LOCATION': wncklet_applet_location,
    },
  )
  install_data(wncklet_service,
    install_dir: join_paths(mate_panel_datadir, 'dbus-1', 'services'),
  )
endif

# gsettings schemas
foreach schema : [
    'org.mate.panel.applet.window-list',
    'org.mate.panel.applet.window-list-previews',
    'org.mate.panel.applet.workspace-switcher',
  ]
  install_data(
    configure_file(
      input: schema + '.gschema.xml.in',
      output: schema + '.gschema.xml',
      configuration: {'GETTEXT_PACKAGE': mate_panel_gettext_package},
    ),
    install_dir: mate_panel_schemadir,
  )
endforeach
