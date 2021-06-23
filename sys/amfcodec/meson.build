amfcodec_sources = [
  'AMF_codec_plugin.cpp',
  'gst-amf.cpp',
  'amf-trace-writer.cpp',
  'gstamfh264enc.cpp',
  'gstamfh265enc.cpp'
]

extra_c_args = ['-DCOBJMACROS', '-DGST_USE_UNSTABLE_API']
extra_cpp_args = ['-DGST_USE_UNSTABLE_API']
extra_dep = [gstd3d11]

if cc.has_header('dxva.h') and cc.has_header('d3d9.h')
  extra_c_args += ['-DHAVE_DXVA_H']
  extra_dep += [gstcodecs_dep]
endif

if d3d11_conf.get('GST_D3D11_DXGI_HEADER_VERSION') >= 5 and d3d11_conf.get('GST_D3D11_HEADER_VERSION') >= 4
  extra_c_args += ['-DHAVE_D3D11_VIDEO_PROC']
endif

if cc.get_id() != 'msvc'
  extra_args = cc.get_supported_arguments([
    '-Wno-redundant-decls',
  ])

  extra_c_args += extra_args
  extra_cpp_args += extra_args
endif

gstamfcodec = library('amfcodec',
  amfcodec_sources,
  c_args : gst_plugins_bad_args + extra_c_args,
  cpp_args: gst_plugins_bad_args + extra_cpp_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep, gstpbutils_dep, gmodule_dep, gstcodecs_dep, gstd3d11_dep],
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gstamfcodec, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstamfcodec]

