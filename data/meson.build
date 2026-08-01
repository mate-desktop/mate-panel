# panel layouts
install_data(
  'default.layout',
  'fedora.layout',
  'linuxmint.layout',
  'mageia.layout',
  'opensuse.layout',
  'ubuntu.layout',
  install_dir: mate_panel_layoutsdir,
)

# gsettings enums
glib_mkenums = find_program('glib-mkenums')

panel_enums = custom_target('org.mate.panel.enums.xml',
  input: '../mate-panel/panel-enums-gsettings.h',
  output: 'org.mate.panel.enums.xml',
  build_by_default: true,
  install: true,
  install_dir: mate_panel_schemadir,
  command: [
    glib_mkenums,
    '--comments', '<!-- @comment@ -->',
    '--fhead', '<schemalist>',
    '--vhead', '  <@type@ id=\'org.mate.panel.@EnumName@\'>',
    '--vprod', '    <value nick=\'@valuenick@\' value=\'@valuenum@\'/>',
    '--vtail', '  </@type@>',
    '--ftail', '</schemalist>',
    '--output', '@OUTPUT@',
    '@INPUT@',
  ],
)

# gsettings schemas
schemas = []
foreach schema : [
    'org.mate.panel',
    'org.mate.panel.object',
    'org.mate.panel.toplevel',
    'org.mate.panel.menubar',
  ]
  schemas += configure_file(
    input: schema + '.gschema.xml.in',
    output: schema + '.gschema.xml',
    configuration: {'GETTEXT_PACKAGE': mate_panel_gettext_package},
  )
endforeach

install_data(schemas, install_dir: mate_panel_schemadir)

glib_compile_schemas = find_program('glib-compile-schemas')

gschemas_compiled = custom_target('gschemas.compiled',
  output: 'gschemas.compiled',
  input: schemas,
  build_by_default: true,
  command: [
    glib_compile_schemas,
    meson.current_build_dir(),
  ],
)

gnome = import('gnome')
gnome.post_install(glib_compile_schemas: true)
