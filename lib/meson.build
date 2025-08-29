lib_src = files()

subdir('aws')

self_lib = static_library(
    's3cpp',
    lib_src,
    dependencies: [
        boost_dep,
        openssl_dep,
        botan_dep,
        pugixml_dep,
        libatomic_dep,
    ],
    include_directories: inc_self,
)

self_dep = declare_dependency(
    link_with: self_lib,
    include_directories: inc_self,
    dependencies: [boost_dep, pugixml_dep],
)
