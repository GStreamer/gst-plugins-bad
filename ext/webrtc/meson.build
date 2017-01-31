webrtc_sources = [
  'gstwebrtc.c',
  'gstwebrtcice.c',
  'icestream.c',
  'nicetransport.c',
  'gstwebrtcbin.c',
  'transportreceivebin.c',
  'transportsendbin.c',
  'transportstream.c',
  'utils.c',
]

libnice_dep = dependency('nice', version : '>=0.1.14', required : false)

if libnice_dep.found()
  library('gstwebrtc',
    webrtc_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [libnice_dep, gstbase_dep, gstsdp_dep, gstwebrtc_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
