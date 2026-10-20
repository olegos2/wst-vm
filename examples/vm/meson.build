raylib_dep = dependency('raylib')

executable(
    'vm',
    sources: files(
        'vm.c',
    ),
    include_directories: inc,
    dependencies: [
        toys_vm_dep,
        raylib_dep,
    ],
    install: true,
)

executable(
    'mandelbulb_ref',
    sources: files(
        'mandelbulb_ref.c',
    ),
    dependencies: [
        m_dep,
    ],
)
