panel_applet_private_inc = include_directories('.')

libmate_panel_applet_private_sources = [
  'panel-applets-manager-dbus.c',
  'panel-applets-manager-dbus.h',
  'panel-applet-container.c',
  'panel-applet-container.h',
  'panel-applet-frame-dbus.c',
  'panel-applet-frame-dbus.h',
]

libmate_panel_applet_private_mini_sources = [
  'panel-applet-mini.c',
  'panel-applets-manager-dbus.c',
  'panel-applets-manager-dbus.h',
  'panel-applet-container.c',
  'panel-applet-container.h',
]

libmate_panel_applet_private_common_args = [
  '-DDATADIR="@0@"'.format(mate_panel_datadir),
  '-DMATE_PANEL_APPLETS_DIR="@0@"'.format(mate_panel_appletsdir),
] + disable_deprecated_flags

libmate_panel_applet_private_inc_dirs = [
  panel_applet_private_inc,
  mate_panel_inc,
  libpanel_util_inc,
]

libmate_panel_applet_private_deps = panel_deps + [dconf_dep]

libmate_panel_applet_private = static_library('mate-panel-applet-private',
  libmate_panel_applet_private_sources,
  panel_marshal,
  include_directories: libmate_panel_applet_private_inc_dirs,
  dependencies: libmate_panel_applet_private_deps,
  c_args: libmate_panel_applet_private_common_args,
)

libmate_panel_applet_private_mini = static_library('mate-panel-applet-private-mini',
  libmate_panel_applet_private_mini_sources,
  panel_marshal,
  include_directories: libmate_panel_applet_private_inc_dirs,
  dependencies: libmate_panel_applet_private_deps,
  c_args: libmate_panel_applet_private_common_args,
)
