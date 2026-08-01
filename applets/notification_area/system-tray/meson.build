na_marshal = gnome.genmarshal('na-marshal',
  sources: 'na-marshal.list',
  prefix: '_na_marshal',
  internal: true,
)

libsystem_tray = static_library('system-tray',
  'fixedtip.c',
  'fixedtip.h',
  na_marshal,
  'na-tray.c',
  'na-tray.h',
  'na-tray-child.c',
  'na-tray-child.h',
  'na-tray-manager.c',
  'na-tray-manager.h',
  include_directories: [include_directories('.'), include_directories('..')],
  dependencies: notification_area_deps + [x11_dep],
  c_args: [
    '-DMATELOCALEDIR="@0@"'.format(mate_panel_localedir),
    '-DG_LOG_DOMAIN="notification-area-applet"',
  ] + disable_deprecated_flags,
)
