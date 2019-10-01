project('gst-tcp-daemon', 'c',
  version : '0.1',
  default_options : ['warning_level=3'])

gst_dep = dependency('gstreamer-1.0')
gst_base_dep = dependency('gstreamer-base-1.0')
gst_plugins_dep = dependency('gstreamer-plugins-base-1.0')
pthread = dependency('threads')

exe = executable('gst_tcp_daemon', 'gst_tcp_daemon.c',
  dependencies: [gst_dep, gst_base_dep, gst_plugins_dep, pthread],
  install : true)

test('basic', exe)
