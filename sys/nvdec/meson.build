nvdec_feature = get_option('nvdec')
if nvdec_feature.enabled()
  nvdec_sources = [
    'gstnvdec.c',
    'plugin.c',
  ]
  nvdec_includes = include_directories('')

  libnvdec_dep = dependency('cuda', version : '>= 6.5', required : false)
  if not libnvdec_dep.found()
    libnvdec_dep = cc.find_library('cuda', required : false)
  endif

  nvdec_c_args = gst_plugins_bad_args

  #check for dynamic lib option
  nvdec_dyn_includes = get_option('nvdec_dyn_lib_includes')
  if nvdec_dyn_includes != ''
    nvdec_sources += [
      'dynlink_nvcuvid.c',
      'dynlink_cuda.c',
      ]
    nvdec_includes = include_directories(nvdec_dyn_includes)
    nvdec_c_args += ['-DHAVE_DYNLINK_HEADERS_NVDEC']
  endif

  #create the library
  nvdec = library('gstnvdec',
    nvdec_sources,
    c_args : nvdec_c_args,
    include_directories : [configinc, nvdec_includes],
    dependencies : [gmodule_dep, gstbase_dep, gstvideo_dep, gstallocators_dep, gstgl_dep, libnvdec_dep],
    install : true,
    install_dir : plugins_install_dir,
  )

  pkgconfig.generate(nvdec, install_dir : plugins_pkgconfig_install_dir)

endif
