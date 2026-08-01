icon_names = [
  'mate-panel-clock',
  'mate-panel-drawer',
  'mate-panel-fish',
  'mate-panel-force-quit',
  'mate-panel-launcher',
  'mate-panel-notification-area',
  'mate-panel-separator',
  'mate-panel',
  'mate-panel-window-list',
  'mate-panel-window-menu',
  'mate-panel-workspace-switcher',
]

foreach size : ['16x16', '22x22', '24x24', '32x32', '48x48', '64x64', '96x96', '256x256']
  icons_dir = join_paths(mate_panel_datadir, 'icons', 'hicolor', size, 'apps')
  install_data(
    sources: [size + '/mate-panel-clock.png',
              size + '/mate-panel-drawer.png',
              size + '/mate-panel-fish.png',
              size + '/mate-panel-force-quit.png',
              size + '/mate-panel-launcher.png',
              size + '/mate-panel-notification-area.png',
              size + '/mate-panel-separator.png',
              size + '/mate-panel.png',
              size + '/mate-panel-window-list.png',
              size + '/mate-panel-window-menu.png',
              size + '/mate-panel-workspace-switcher.png'],
    install_dir: icons_dir,
  )
endforeach

install_data(
  'scalable/mate-panel-clock.svg',
  'scalable/mate-panel-drawer.svg',
  'scalable/mate-panel-fish.svg',
  'scalable/mate-panel-force-quit.svg',
  'scalable/mate-panel-launcher.svg',
  'scalable/mate-panel-notification-area.svg',
  'scalable/mate-panel-separator.svg',
  'scalable/mate-panel.svg',
  'scalable/mate-panel-window-list.svg',
  'scalable/mate-panel-window-menu.svg',
  'scalable/mate-panel-workspace-switcher.svg',
  install_dir: join_paths(mate_panel_datadir, 'icons', 'hicolor', 'scalable', 'apps'),
)

gnome = import('gnome')

gnome.post_install(
  glib_compile_schemas: false,
  gtk_update_icon_cache: true,
)
