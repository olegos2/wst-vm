project('wst-vm', 'c', version: '0.1', meson_version: '>= 1.1.0')

add_project_arguments(
    '-Wall',
    '-Wextra',
    '-Wconversion',
    '-Wfloat-equal',
    '-Wshadow',
    '-Wpointer-arith',
    '-Wno-unused-function',
    language: 'c'
)

if get_option('debug')
    add_project_arguments('-DWST_DEBUG', language: 'c')
endif

inc = include_directories(
    'include',
)

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required: false)

wst_toys = subproject(
    'wst-toys',
    default_options: [
        'examples=false',
        'tests=false',
    ],
)
toys_common_dep = wst_toys.get_variable('toys_common_dep')

subdir('src')

if get_option('examples')
    subdir('examples')
endif
