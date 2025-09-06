lib_bindings_py_src = files('s3cpp.cpp')

subdir('aws')

mod_bindings_py = py_installation.extension_module(
    's3cpp',
    lib_bindings_py_src,
    dependencies: [s3cpp_dep, boost_dep, python_dep, nanobind_dep],
    install: true,
    gnu_symbol_visibility: 'inlineshidden',
)

py_installation.install_sources(['py.typed'], subdir: 's3cpp')
