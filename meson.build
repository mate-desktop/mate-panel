project(
  'mate-panel',
  'c',
  version: '1.29.0',
  meson_version: '>= 0.56.0',
  license: ['GPL-2', 'LGPL-2', 'FDL-1.1'],
)

# Before making a release, the LT_VERSION string should be modified.
# The string is of the form C:R:A.
# - If interfaces have been changed or added, but binary compatibility has
#   been preserved, change to C+1:0:A+1
# - If binary compatibility has been broken (eg removed or changed interfaces)
#   change to C+1:0:0
# - If the interface is the same as the previous version, change to C:R+1:A
libmate_panel_applet_current = 2
libmate_panel_applet_revision = 0
libmate_panel_applet_age = 0

mate_panel_version = meson.project_version()
version_array = mate_panel_version.split('.')
mate_panel_major_version = version_array[0].to_int()
mate_panel_minor_version = version_array[1].to_int()
mate_panel_micro_version = version_array[2].to_int()

mate_panel_api_version = '4.0'
mate_panel_api_name = 'libmate-panel-applet-@0@'.format(mate_panel_api_version)

mate_panel_gir_ns = 'MatePanelApplet'
mate_panel_gir_version = mate_panel_api_version

mate_panel_gettext_package = meson.project_name()

mate_panel_prefix = get_option('prefix')
mate_panel_bindir = join_paths(mate_panel_prefix, get_option('bindir'))
mate_panel_datadir = join_paths(mate_panel_prefix, get_option('datadir'))
mate_panel_includedir = join_paths(mate_panel_prefix, get_option('includedir'))
mate_panel_libdir = join_paths(mate_panel_prefix, get_option('libdir'))
mate_panel_libexecdir = join_paths(mate_panel_prefix, get_option('libexecdir'))
mate_panel_localedir = join_paths(mate_panel_prefix, get_option('localedir'))
mate_panel_pkgdatadir = join_paths(mate_panel_datadir, 'mate-panel')
mate_panel_pkglibdir = join_paths(mate_panel_libdir, 'mate-panel')
mate_panel_modulesdir = join_paths(mate_panel_libdir, 'mate-panel', 'modules')
mate_panel_appletsdir = join_paths(mate_panel_datadir, 'mate-panel', 'applets')
mate_panel_layoutsdir = join_paths(mate_panel_pkgdatadir, 'layouts')
mate_panel_schemadir = join_paths(mate_panel_datadir, 'glib-2.0', 'schemas')

cc = meson.get_compiler('c')

# options
enable_deprecation_flags = get_option('enable-deprecation-flags')
enable_introspection = get_option('introspection')
enable_gtk_doc = get_option('gtk-doc')
enable_help = get_option('help')
enable_compile_warnings = get_option('compile-warnings')

config_h = configuration_data()
config_h.set_quoted('GETTEXT_PACKAGE', mate_panel_gettext_package)
config_h.set_quoted('PACKAGE', meson.project_name())
config_h.set_quoted('PACKAGE_NAME', meson.project_name())
config_h.set_quoted('PACKAGE_STRING', '@0@ @1@'.format(meson.project_name(), mate_panel_version))
config_h.set_quoted('PACKAGE_TARNAME', meson.project_name())
config_h.set_quoted('PACKAGE_URL', 'https://mate-desktop.org')
config_h.set_quoted('PACKAGE_BUGREPORT', 'https://github.com/mate-desktop/mate-panel/issues')
config_h.set_quoted('PACKAGE_VERSION', mate_panel_version)
config_h.set_quoted('VERSION', mate_panel_version)
config_h.set_quoted('MATELOCALEDIR', mate_panel_localedir)
config_h.set_quoted('DATADIR', mate_panel_datadir)
config_h.set_quoted('PANELDATADIR', mate_panel_pkgdatadir)
config_h.set_quoted('PANEL_MODULES_DIR', mate_panel_modulesdir)
config_h.set_quoted('MATE_PANEL_APPLETS_DIR', mate_panel_appletsdir)
config_h.set('ENABLE_NLS', 1)

# standard headers
foreach header : [
    'dirent.h',
    'dlfcn.h',
    'inttypes.h',
    'langinfo.h',
    'stdint.h',
    'stdio.h',
    'stdlib.h',
    'strings.h',
    'string.h',
    'sys/stat.h',
    'sys/types.h',
    'unistd.h',
  ]
  if cc.has_header(header)
    config_h.set('HAVE_' + header.underscorify().to_upper(), 1)
  endif
endforeach
config_h.set('HAVE_NL_LANGINFO', cc.has_function('nl_langinfo'))
config_h.set('STDC_HEADERS', 1)
config_h.set('HAVE_STRUCT_DIRENT_D_TYPE',
  cc.has_member('struct dirent', 'd_type', prefix: '#include <dirent.h>'))

if get_option('enable-debug').enabled()
  config_h.set('MATE_ENABLE_DEBUG', 1)
else
  config_h.set('NDEBUG', 1)
endif

# compile warnings
warning_flags = []
if enable_compile_warnings == 'no'
  warning_flags = []
elif enable_compile_warnings == 'minimum'
  warning_flags = ['-Wall']
elif enable_compile_warnings == 'yes'
  warning_flags = ['-Wall', '-Wmissing-prototypes']
elif enable_compile_warnings == 'maximum' or enable_compile_warnings == 'error'
  warning_flags = [
    '-Wall',
    '-Wmissing-prototypes',
    '-Wbad-function-cast',
    '-Wcast-align',
    '-Wextra',
    '-Wno-unused-parameter',
    '-Wformat-nonliteral',
    '-Wmissing-declarations',
    '-Wmissing-field-initializers',
    '-Wnested-externs',
    '-Wpointer-arith',
    '-Wredundant-decls',
    '-Wshadow',
    '-Wstrict-prototypes',
    '-Werror=format-security',
  ]
  if enable_compile_warnings == 'error'
    warning_flags += ['-Werror']
  endif
endif

foreach flag : warning_flags
  if cc.has_argument(flag)
    add_project_arguments(flag, language: 'c')
  endif
endforeach

# *_DISABLE_DEPRECATED flags
disable_deprecated_flags = []
if enable_deprecation_flags
  disable_deprecated_flags = [
    '-DG_DISABLE_DEPRECATED',
    '-DG_DISABLE_SINGLE_INCLUDES',
    '-DATK_DISABLE_DEPRECATED',
    '-DATK_DISABLE_SINGLE_INCLUDES',
    '-DPANGO_DISABLE_DEPRECATED',
    '-DPANGO_DISABLE_SINGLE_INCLUDES',
    '-DGDK_DISABLE_DEPRECATED',
    '-DGDK_DISABLE_SINGLE_INCLUDES',
    '-DGDK_PIXBUF_DISABLE_DEPRECATED',
    '-DGDK_PIXBUF_DISABLE_SINGLE_INCLUDES',
    '-DGTK_DISABLE_DEPRECATED',
    '-DGTK_DISABLE_SINGLE_INCLUDES',
    '-DMATE_DISABLE_DEPRECATED',
    '-DMATE_DISABLE_SINGLE_INCLUDES',
    '-DLIBGLADE_DISABLE_DEPRECATED',
    '-DLIBGLADE_DISABLE_SINGLE_INCLUDES',
    '-DVTE_DISABLE_DEPRECATED',
    '-DVTE_DISABLE_SINGLE_INCLUDES',
    '-DWNCK_DISABLE_DEPRECATED',
    '-DWNCK_DISABLE_SINGLE_INCLUDES',
    '-DLIBSOUP_DISABLE_DEPRECATED',
    '-DLIBSOUP_DISABLE_SINGLE_INCLUDES',
  ]
endif

add_project_arguments(
  '-DHAVE_CONFIG_H',
  '-I' + meson.current_build_dir(),
  language: 'c',
)

# dependencies
glib_dep = dependency('glib-2.0', version: '>= 2.50.0')
gio_dep = dependency('gio-2.0', version: '>= 2.50.0')
gio_unix_dep = dependency('gio-unix-2.0', version: '>= 2.50.0')
gdk_pixbuf_dep = dependency('gdk-pixbuf-2.0', version: '>= 2.26.0')
pango_dep = dependency('pango', version: '>= 1.15.4')
gtk_dep = dependency('gtk+-3.0', version: '>= 3.24.0')
cairo_dep = dependency('cairo', version: '>= 1.0.0')
mate_desktop_dep = dependency('mate-desktop-2.0', version: '>= 1.28.2')
mate_menu_dep = dependency('libmate-menu', version: '>= 1.21.0')
dconf_dep = dependency('dconf', version: '>= 0.13.4')
wnck_dep = dependency('libwnck-3.0', version: '>= 43.0')
mateweather_dep = dependency('mateweather', version: '>= 1.17.0')
gmodule_dep = dependency('gmodule-2.0', required: false)
ice_dep = dependency('ice')
sm_dep = dependency('sm')
m_dep = cc.find_library('m', required: false)

# X11 support
x11_opt = get_option('enable-x11')
x11_dep = dependency('x11', required: x11_opt)
have_x11 = x11_dep.found()
config_h.set('HAVE_X11', have_x11)

# Wayland support
wayland_opt = get_option('enable-wayland')
wayland_deps = [
  dependency('gtk-layer-shell-0', required: wayland_opt),
  dependency('wayland-client', required: wayland_opt),
]
have_wayland = wayland_deps[0].found() and wayland_deps[1].found()
if have_wayland
  xfw_dep = dependency('libxfce4windowing-0', version: '>= 4.19.4', required: false)
  if not xfw_dep.found()
    if wayland_opt.enabled()
      error('Wayland support requires libxfce4windowing-0 >= 4.19.4')
    else
      have_wayland = false
    endif
  endif
endif
config_h.set('HAVE_WAYLAND', have_wayland)

if not have_x11 and not have_wayland
  if wayland_opt.disabled() and x11_opt.disabled()
    error('At least one backend must be enabled')
  else
    error('No usable backend found, install X11 or Wayland development libraries')
  endif
endif

# XRandR extension
randr_dep = dependency('xrandr', version: '>= 1.3.0', required: false)
have_randr = randr_dep.found()
config_h.set('HAVE_RANDR', have_randr)

# Evolution Data Server support for calendar integration
eds_opt = get_option('enable-eds')
eds_deps = [
  dependency('libecal-2.0', version: '>= 3.33.2', required: eds_opt),
  dependency('libedataserver-1.2', version: '>= 3.5.3', required: eds_opt),
]
have_eds = eds_deps[0].found() and eds_deps[1].found()
config_h.set('HAVE_EDS', have_eds)

# applets to compile in-process
in_process_applets = get_option('in-process-applets')
clock_inprocess = false
fish_inprocess = false
notification_area_inprocess = false
wncklet_inprocess = false
if 'none' in in_process_applets
  in_process_applets = []
endif
if 'all' in in_process_applets
  in_process_applets = ['clock', 'fish', 'notification-area', 'wncklet']
endif
foreach applet : in_process_applets
  if applet == 'clock'
    clock_inprocess = true
    config_h.set('CLOCK_INPROCESS', 1)
  elif applet == 'fish'
    fish_inprocess = true
    config_h.set('FISH_INPROCESS', 1)
  elif applet == 'notification-area'
    notification_area_inprocess = true
    config_h.set('NOTIFICATION_AREA_INPROCESS', 1)
  elif applet == 'wncklet'
    wncklet_inprocess = true
    config_h.set('WNCKLET_INPROCESS', 1)
  else
    error('applet @0@ not recognized'.format(applet))
  endif
endforeach

panel_deps = [
  gmodule_dep,
  gdk_pixbuf_dep,
  pango_dep,
  gtk_dep,
  glib_dep,
  gio_unix_dep,
  mate_desktop_dep,
  gio_dep,
  mate_menu_dep,
]

libmate_panel_applet_deps = [
  gtk_dep,
  gio_dep,
  gio_unix_dep,
]

# top-level include dirs
panel_inc = include_directories('.')

subdir('po')
subdir('data')
subdir('icons')
subdir('man')
subdir('libmate-panel-applet')
subdir('mate-panel')
subdir('applets')

gtk_doc_dep = dependency('gtk-doc', required: enable_gtk_doc)
if gtk_doc_dep.found()
  subdir('doc')
endif

itstool = find_program('itstool', required: enable_help)
if enable_help.allowed() and itstool.found()
  subdir('help')
endif

configure_file(
  output: 'config.h',
  configuration: config_h,
)

summary({
  'Maintainer mode': false,
  'Use *_DISABLE_DEPRECATED': enable_deprecation_flags,
  'Applets to build in-process': in_process_applets.length() > 0 ? in_process_applets : ['(none)'],
  'Wayland support': have_wayland,
  'X11 support': have_x11,
  'XRandr support': have_randr,
  'Evolution Data Server support': have_eds,
  'Build introspection support': enable_introspection,
  'Build gtk-doc documentation': enable_gtk_doc,
}, section: 'mate-panel')
