stubgen_tgts += custom_target(
    'stubgen.s3cpp.aws.s3',
    command: [
        stubgen_exe,
        '-m',
        's3cpp.aws.s3',
        '-i',
        fs.parent(mod_bindings_py.full_path()),
        '-o',
        '@OUTPUT@',
    ],
    depends: mod_bindings_py,
    output: '__init__.pyi',
    install: true,
    install_dir: stub_install_dir / 'aws' / 's3',
    install_tag: 'python-runtime',
)
