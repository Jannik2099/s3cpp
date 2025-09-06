stubgen_tgts += custom_target(
    'stubgen.s3cpp.aws',
    command: [
        stubgen_exe,
        '-m',
        's3cpp.aws',
        '-i',
        fs.parent(mod_bindings_py.full_path()),
        '-o',
        '@OUTPUT@',
    ],
    depends: mod_bindings_py,
    output: '__init__.pyi',
    install: true,
    install_dir: stub_install_dir / 'aws',
    install_tag: 'python-runtime',
)
subdir('iam')
subdir('s3')
