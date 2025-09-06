stubgen_exe = nanobind_dep.get_variable(internal: 'stubgen_file')
stub_install_dir = py_installation.get_install_dir() / 's3cpp'

stubgen_tgts = []

stubgen_tgts += custom_target(
    'stubgen.s3cpp',
    command: [
        stubgen_exe,
        '-m',
        's3cpp',
        '-i',
        fs.parent(mod_bindings_py.full_path()),
        '-o',
        '@OUTPUT@',
    ],
    depends: mod_bindings_py,
    output: '__init__.pyi',
    install: true,
    install_dir: stub_install_dir,
    install_tag: 'python-runtime',
)

subdir('aws')
