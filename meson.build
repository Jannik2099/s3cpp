project(
    's3cpp',
    'cpp',
    version: '0.1',
    default_options: ['warning_level=3', 'cpp_std=c++26'],
    meson_version: '>=1.1',
)

cmake = import('cmake')
cxx = meson.get_compiler('cpp')

exe_link_args = []
prefer_static = get_option('prefer_static')
if prefer_static
    exe_link_args += ['-static-libstdc++', '-static-libgcc']
endif

add_project_arguments(
    cxx.get_supported_arguments(
        '-Wno-missing-designated-field-initializers',
        '-Wno-missing-field-initializers',
    ),
    language: 'cpp',
)
add_project_arguments(
    '-DBOOST_ASIO_HAS_IO_URING',
    language: 'cpp',
)

uring_dep = dependency(
    'liburing',
    include_type: 'system',
    static: prefer_static,
)
openssl_dep = dependency(
    'openssl',
    include_type: 'system',
    static: prefer_static,
)
botan_dep = dependency(
    'botan-3',
    include_type: 'system',
    static: prefer_static,
)
pugixml_dep = dependency(
    'pugixml',
    include_type: 'system',
    static: prefer_static,
)

boost_modules = ['program_options', 'url']
boost_dep = dependency(
    'boost',
    modules: boost_modules,
    include_type: 'system',
    required: false,
    static: prefer_static,
    # we need to disable the fallback here, as a cmake wrap cannot specify cmake options yet
    allow_fallback: false,
)
if not boost_dep.found()
    boost_components = [
        'accumulators',
        'algorithm',
        'asio',
        'beast',
        'describe',
    ] + boost_modules
    cmake_options = cmake.subproject_options()
    cmake_options.add_cmake_defines(
        {
            'BOOST_INCLUDE_LIBRARIES': ';'.join(boost_components),
        },
    )
    cmake_options.append_compile_args('c', '-w')
    cmake_options.append_compile_args('cpp', '-w')
    boost_cmake_dep = cmake.subproject(
        'boost',
        options: cmake_options,
    )

    boost_cmake_libs = []
    foreach component : boost_components
        boost_cmake_libs += boost_cmake_dep.dependency(
            'boost_' + component,
            include_type: 'system',
        )
    endforeach

    boost_dep = declare_dependency(
        dependencies: boost_cmake_libs,
    )
endif

subdir('include')
subdir('lib')
subdir('src')

subdir('test')
