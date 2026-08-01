mate_panel_inc = include_directories('.')
mate_submodules_inc = include_directories('mate-submodules')
libpanel_util_inc = include_directories('libpanel-util')

glib_mkenums = find_program('glib-mkenums')
glib_compile_resources = find_program('glib-compile-resources')

# --- generated sources ---

# marshallers
panel_marshal = gnome.genmarshal('panel-marshal',
  sources: 'panel-marshal.list',
  prefix: 'panel_marshal',
)

# enumerations
panel_enum_headers = [
  'panel-enums.h',
  'panel-enums-gsettings.h',
  'panel-types.h',
]

panel_typebuiltins_c = custom_target('panel-typebuiltins.c',
  input: panel_enum_headers,
  output: 'panel-typebuiltins.c',
  command: [
    glib_mkenums,
    '--fhead', '#include <glib-object.h>\n',
    '--fhead', '#include "panel-typebuiltins.h"\n\n',
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

panel_typebuiltins_h = custom_target('panel-typebuiltins.h',
  input: panel_enum_headers,
  output: 'panel-typebuiltins.h',
  command: [
    glib_mkenums,
    '--fhead', '#ifndef __PANEL_TYPEBUILTINS_H__\n',
    '--fhead', '#define __PANEL_TYPEBUILTINS_H__ 1\n\n',
    '--fhead', '#ifdef __cplusplus\nextern "C" {\n#endif\n\n',
    '--ftail', '#ifdef __cplusplus\n}\n#endif\n\n',
    '--ftail', '#endif /* __PANEL_TYPEBUILTINS_H__ */\n',
    '--fprod', '\n/* --- @filename@ --- */',
    '--eprod', '#define PANEL_TYPE_@ENUMSHORT@ @enum_name@_get_type()\n',
    '--eprod', 'GType @enum_name@_get_type (void);\n',
    '--output', '@OUTPUT@',
    '@INPUT@',
  ],
)

# resources
panel_resource_deps = [
  'panel-ditem-editor-dialog.ui',
  'panel-properties-dialog.ui',
  'panel-run-dialog.ui',
  '../data/theme/mate-panel.css',
  '../data/theme/panel-grid-symbolic.svg',
]

panel_resources_c = custom_target('panel-resources.c',
  input: 'panel.gresource.xml',
  output: 'panel-resources.c',
  depend_files: panel_resource_deps,
  command: [
    glib_compile_resources,
    '--target', '@OUTPUT@',
    '--sourcedir', meson.current_source_dir(),
    '--generate-source',
    '--c-name', 'panel',
    '@INPUT@',
  ],
)

panel_resources_h = custom_target('panel-resources.h',
  input: 'panel.gresource.xml',
  output: 'panel-resources.h',
  depend_files: panel_resource_deps,
  command: [
    glib_compile_resources,
    '--target', '@OUTPUT@',
    '--sourcedir', meson.current_source_dir(),
    '--generate-header',
    '--c-name', 'panel',
    '@INPUT@',
  ],
)

panel_test_applets_resources_c = custom_target('panel-test-applets-resources.c',
  input: 'panel-test-applets.gresource.xml',
  output: 'panel-test-applets-resources.c',
  command: [
    glib_compile_resources,
    '--target', '@OUTPUT@',
    '--sourcedir', meson.current_source_dir(),
    '--generate-source',
    '--c-name', 'panel',
    '@INPUT@',
  ],
)

panel_test_applets_resources_h = custom_target('panel-test-applets-resources.h',
  input: 'panel-test-applets.gresource.xml',
  output: 'panel-test-applets-resources.h',
  command: [
    glib_compile_resources,
    '--target', '@OUTPUT@',
    '--sourcedir', meson.current_source_dir(),
    '--generate-header',
    '--c-name', 'panel',
    '@INPUT@',
  ],
)

# --- sub-libraries ---

# libegg is built from the mate-submodules git submodule, whose working tree
# is not tracked by this repository.  Build it directly from here so that a
# fresh checkout (with the submodule populated) needs no extra meson files.
libegg_sources = files(
  'mate-submodules/libegg/eggtreemultidnd.c',
  'mate-submodules/libegg/eggtreemultidnd.h',
  'mate-submodules/libegg/eggdesktopfile.c',
  'mate-submodules/libegg/eggdesktopfile.h',
  'mate-submodules/libegg/eggsmclient.c',
  'mate-submodules/libegg/eggsmclient.h',
  'mate-submodules/libegg/eggsmclient-private.h',
  'mate-submodules/libegg/eggsmclient-xsmp.c',
)

libegg = static_library('egg',
  libegg_sources,
  dependencies: [ice_dep, sm_dep, gtk_dep, glib_dep],
  c_args: [
    '-DEGG_SM_CLIENT_BACKEND_XSMP',
    '-DG_LOG_DOMAIN="EggSMClient"',
  ] + disable_deprecated_flags,
)

subdir('libmate-panel-applet-private')
subdir('libpanel-util')

# --- programs ---

panel_common_cpp_args = [
  '-DMATELOCALEDIR="@0@"'.format(mate_panel_localedir),
  '-DPANELDATADIR="@0@"'.format(mate_panel_pkgdatadir),
  '-DPANEL_MODULES_DIR="@0@"'.format(mate_panel_modulesdir),
] + disable_deprecated_flags

panel_common_inc_dirs = [
  mate_panel_inc,
  libpanel_util_inc,
  mate_submodules_inc,
]

panel_common_deps = panel_deps + [dconf_dep]

panel_sources = [
  panel_marshal,
  panel_typebuiltins_c,
  panel_typebuiltins_h,
  panel_resources_c,
  panel_resources_h,
  'main.c',
  'panel-widget.c',
  'button-widget.c',
  'panel-session.c',
  'panel.c',
  'applet.c',
  'drawer.c',
  'panel-config-global.c',
  'panel-util.c',
  'panel-properties-dialog.c',
  'panel-run-dialog.c',
  'menu.c',
  'panel-context-menu.c',
  'launcher.c',
  'panel-applet-frame.c',
  'panel-applets-manager.c',
  'panel-shell.c',
  'panel-background.c',
  'panel-stock-icons.c',
  'panel-action-button.c',
  'panel-menu-bar.c',
  'panel-menu-button.c',
  'panel-menu-items.c',
  'panel-separator.c',
  'panel-recent.c',
  'panel-toplevel.c',
  'panel-frame.c',
  'panel-multimonitor.c',
  'panel-a11y.c',
  'panel-bindings.c',
  'panel-layout.c',
  'panel-profile.c',
  'panel-lockdown.c',
  'panel-addto.c',
  'panel-ditem-editor.c',
  'panel-modules.c',
  'panel-applet-info.c',
  'panel-reset.c',
  'panel-types.h',
  'panel-widget.h',
  'panel-globals.h',
  'button-widget.h',
  'panel-session.h',
  'panel.h',
  'applet.h',
  'drawer.h',
  'drawer-private.h',
  'panel-util.h',
  'panel-properties-dialog.h',
  'panel-config-global.h',
  'panel-run-dialog.h',
  'menu.h',
  'panel-context-menu.h',
  'launcher.h',
  'panel-applet-frame.h',
  'panel-applets-manager.h',
  'panel-shell.h',
  'panel-background.h',
  'panel-stock-icons.h',
  'panel-action-button.h',
  'panel-menu-bar.h',
  'panel-menu-button.h',
  'panel-menu-items.h',
  'panel-separator.h',
  'panel-recent.h',
  'panel-toplevel.h',
  'panel-frame.h',
  'panel-multimonitor.h',
  'panel-a11y.h',
  'panel-bindings.h',
  'panel-layout.h',
  'panel-profile.h',
  'panel-enums-gsettings.h',
  'panel-enums.h',
  'panel-lockdown.h',
  'panel-addto.h',
  'panel-ditem-editor.h',
  'panel-icon-names.h',
  'panel-modules.h',
  'panel-applet-info.h',
  'panel-reset.h',
  'panel-schemas.h',
]

if have_wayland
  panel_sources += ['wayland-backend.c', 'wayland-backend.h']
endif

if have_x11
  panel_sources += [
    'xstuff.c',
    'panel-xutils.c',
    'panel-force-quit.c',
    'panel-action-protocol.c',
    'panel-struts.c',
    'xstuff.h',
    'panel-xutils.h',
    'panel-force-quit.h',
    'panel-action-protocol.h',
    'panel-struts.h',
  ]
endif

mate_panel_deps = panel_common_deps + [randr_dep]
if have_x11
  mate_panel_deps += x11_dep
endif
if have_wayland
  mate_panel_deps += wayland_deps
endif

executable('mate-panel',
  panel_sources,
  include_directories: panel_common_inc_dirs,
  dependencies: mate_panel_deps,
  c_args: panel_common_cpp_args + ['-DMATEMENU_I_KNOW_THIS_IS_UNSTABLE'],
  link_with: [libegg, libmate_panel_applet_private, libpanel_util],
  export_dynamic: true,
  install: true,
)

mate_desktop_item_edit_sources = [
  'mate-desktop-item-edit.c',
  'panel-ditem-editor.c',
  panel_resources_c,
  panel_resources_h,
  panel_marshal,
  'panel-util.c',
]

if have_x11
  mate_desktop_item_edit_sources += ['xstuff.c']
endif

mate_desktop_item_edit_deps = panel_common_deps
if have_x11
  mate_desktop_item_edit_deps += x11_dep
endif

executable('mate-desktop-item-edit',
  mate_desktop_item_edit_sources,
  include_directories: panel_common_inc_dirs,
  dependencies: mate_desktop_item_edit_deps,
  c_args: panel_common_cpp_args,
  link_with: libpanel_util,
  export_dynamic: true,
  install: true,
)

mate_panel_test_applets_sources = [
  panel_marshal,
  panel_test_applets_resources_c,
  panel_test_applets_resources_h,
  'panel-modules.c',
  'panel-applet-info.c',
  'panel-applets-manager.c',
  'panel-test-applets.c',
]

executable('mate-panel-test-applets',
  mate_panel_test_applets_sources,
  include_directories: panel_common_inc_dirs,
  dependencies: panel_common_deps,
  c_args: panel_common_cpp_args + [
    '-DMATE_PANEL_APPLETS_DIR="@0@"'.format(mate_panel_appletsdir),
  ],
  link_with: [libmate_panel_applet_private_mini, libpanel_util],
  export_dynamic: true,
  install: true,
)

# desktop file
mate_panel_desktop = i18n.merge_file(
  input: 'mate-panel.desktop.in',
  output: 'mate-panel.desktop',
  type: 'desktop',
  po_dir: '../po',
  install: true,
  install_dir: join_paths(mate_panel_datadir, 'applications'),
)
