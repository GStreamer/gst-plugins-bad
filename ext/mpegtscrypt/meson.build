mpegtscrypt_src = [
  'gstmpegtscrypt.c',
  'crypt.c'
  ]
dvbcsa_dep = cc.find_library('dvbcsa', required : get_option('mpegtscrypt'))
libcrypto_dep = dependency('libcrypto', required : get_option('mpegtscrypt'))

if dvbcsa_dep.found() and libcrypto_dep.found()
  gstmpegtscrypt = library('gstmpegtscrypt',
    mpegtscrypt_src,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gst_dep, gstbase_dep, dvbcsa_dep, libcrypto_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstmpegtscrypt, install_dir : plugins_pkgconfig_install_dir)
  plugins += [gstmpegtscrypt]
endif
