notification_area_deps = [
  gtk_dep,
  mate_desktop_dep,
]

notification_area_c_args = [
  '-DMATELOCALEDIR="@0@"'.format(mate_panel_localedir),
  '-DG_LOG_DOMAIN="notification-area-applet"',
  '-DPROVIDE_WATCHER_SERVICE=1',
] + disable_deprecated_flags

subdir('libstatus-notifier-watcher')
subdir('status-notifier')

if have_x11
  subdir('system-tray')
endif

libtray_sources = [
  'na-grid.c',
  'na-grid.h',
  'na-host.c',
  'na-host.h',
  'na-item.c',
  'na-item.h',
]

libtray = static_library('tray',
  libtray_sources + status_notifier_codegen,
  include_directories: [include_directories('.'), include_directories('status-notifier')],
  dependencies: notification_area_deps,
  link_with: [libstatus_notifier, libstatus_notifier_watcher],
  c_args: notification_area_c_args,
)

if have_x11
  libtray_extra_link = libsystem_tray
  executable('testtray',
    'testtray.c',
    include_directories: include_directories('.'),
    dependencies: notification_area_deps,
    link_with: [libtray, libstatus_notifier, libstatus_notifier_watcher, libsystem_tray],
    c_args: notification_area_c_args,
  )
else
  libtray_extra_link = []
endif

na_resource_deps = files(
  'notification-area-preferences-dialog.ui',
  'notification-area-menu.xml',
)

na_resources_c = custom_target('na-resources.c',
  input: 'na.gresource.xml',
  output: 'na-resources.c',
  depend_files: na_resource_deps,
  command: [
    glib_compile_resources,
    '--target', '@OUTPUT@',
    '--sourcedir', meson.current_source_dir(),
    '--generate-source',
    '--c-name', 'na',
    '@INPUT@',
  ],
)

na_resources_h = custom_target('na-resources.h',
  input: 'na.gresource.xml',
  output: 'na-resources.h',
  depend_files: na_resource_deps,
  command: [
    glib_compile_resources,
    '--target', '@OUTPUT@',
    '--sourcedir', meson.current_source_dir(),
    '--generate-header',
    '--c-name', 'na',
    '@INPUT@',
  ],
)

notification_area_sources = [
  'main.c',
  'main.h',
  na_resources_c,
  na_resources_h,
]

notification_area_full_deps = notification_area_deps + libmate_panel_applet_deps

notification_area_inc_dirs = [
  include_directories('.'),
  libmate_panel_applet_inc,
]

if notification_area_inprocess
  notification_area_applet = shared_library('notification-area-applet',
    notification_area_sources,
    include_directories: notification_area_inc_dirs,
    dependencies: notification_area_full_deps,
    c_args: notification_area_c_args,
    link_with: [libmate_panel_applet_lib, libtray, libstatus_notifier, libstatus_notifier_watcher] + libtray_extra_link,
    install: true,
    install_dir: mate_panel_pkglibdir,
  )
  notification_area_applet_in_process = 'true'
  notification_area_applet_location = join_paths(mate_panel_pkglibdir, 'libnotification-area-applet.so')
else
  notification_area_applet = executable('notification-area-applet',
    notification_area_sources,
    include_directories: notification_area_inc_dirs,
    dependencies: notification_area_full_deps,
    c_args: notification_area_c_args,
    link_with: [libmate_panel_applet_lib, libtray, libstatus_notifier, libstatus_notifier_watcher] + libtray_extra_link,
    install: true,
    install_dir: mate_panel_libexecdir,
  )
  notification_area_applet_in_process = 'false'
  notification_area_applet_location = join_paths(mate_panel_libexecdir, 'notification-area-applet')
endif

# applet registration file
notification_area_applet_desktop_in = configure_file(
  input: 'org.mate.panel.NotificationAreaApplet.mate-panel-applet.desktop.in.in',
  output: 'org.mate.panel.NotificationAreaApplet.mate-panel-applet.desktop.in',
  configuration: {
    'IN_PROCESS': notification_area_applet_in_process,
    'LOCATION': notification_area_applet_location,
    'VERSION': mate_panel_version,
  },
)

notification_area_applet_desktop = i18n.merge_file(
  input: notification_area_applet_desktop_in,
  output: 'org.mate.panel.NotificationAreaApplet.mate-panel-applet',
  type: 'desktop',
  po_dir: '../../po',
  install: true,
  install_dir: mate_panel_appletsdir,
)

if not notification_area_inprocess
  notification_area_service = configure_file(
    input: 'org.mate.panel.applet.NotificationAreaAppletFactory.service.in',
    output: 'org.mate.panel.applet.NotificationAreaAppletFactory.service',
    configuration: {
      'LOCATION': notification_area_applet_location,
    },
  )
  install_data(notification_area_service,
    install_dir: join_paths(mate_panel_datadir, 'dbus-1', 'services'),
  )
endif

# gsettings schema
notification_area_gschema = configure_file(
  input: 'org.mate.panel.applet.notification-area.gschema.xml.in',
  output: 'org.mate.panel.applet.notification-area.gschema.xml',
  configuration: {'GETTEXT_PACKAGE': mate_panel_gettext_package},
)

install_data(notification_area_gschema, install_dir: mate_panel_schemadir)
