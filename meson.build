# gtklock-playerctl-module
# Copyright (c) 2024 Jovan Lanik

project(
  'gtklock-playerctl-module', 'c',
  version : '4.0.0',
  license : 'GPLv3',
  default_options : ['c_std=c11'],
)

gtk = dependency('gtk+-3.0')
gmodule_export = dependency('gmodule-export-2.0')
playerctl = dependency('playerctl')
libsoup = dependency('libsoup-3.0')

dependencies = [
  gtk,
  gmodule_export,
  playerctl,
  libsoup,
]

gtklock_playerctl_module_sources = files('source.c')

gtklock_playerctl_module_set = [
  gtklock_playerctl_module_sources,
]

if import('fs').is_absolute(get_option('libdir'))
  gtklock_module_dir = get_option('libdir') / 'gtklock'
else
  gtklock_module_dir = get_option('prefix') / get_option('libdir') / 'gtklock'
endif

shared_library(
  'playerctl-module',
  gtklock_playerctl_module_set,
  name_prefix : '',
  dependencies : dependencies,
  install : true,
  install_dir : gtklock_module_dir,
)
