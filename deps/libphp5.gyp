{
  'includes': [ 'common-libphp5.gypi' ],
  # these are libraries not needed on osx
  'variables': { 'libcrypt%':'-lcrypt -lrt -lnsl' },
  'target_defaults': {
    'default_configuration': 'Release',
    'configurations': {
      'Debug': {
        'variables': {
          'configure_options': ['--enable-debug']
        },
        'defines': [ 'DEBUG', '_DEBUG' ],
        'msvs_settings': {
          'VCCLCompilerTool': {
            'RuntimeLibrary': 1, # static debug
          },
        },
      },
      'Release': {
        'variables': {
          'configure_options': ['--enable-release']
        },
        'defines': [ 'NDEBUG' ],
        'msvs_settings': {
          'VCCLCompilerTool': {
            'RuntimeLibrary': 0, # static release
          },
        },
      }
    },
    'msvs_settings': {
      'VCCLCompilerTool': {
      },
      'VCLibrarianTool': {
      },
      'VCLinkerTool': {
        'GenerateDebugInformation': 'true',
      },
    },
    'xcode_settings': {
      'MACOSX_DEPLOYMENT_TARGET': '10.7',

      'OTHER_CFLAGS': [
        '-std=c++11',
        '-stdlib=libc++'
      ],
    },
    'conditions': [
      ['OS == "win"', {
        'defines': [
          'WIN32'
        ],
      }]
    ],
  },

  'targets': [
    {
      'target_name': 'action_before_build',
      'type': 'none',
      'hard_dependency': 1,
      'actions': [
        {
          'action_name': 'unpack_libphp5_dep',
          'inputs': [
            './php-<@(libphp5_version).tar.gz'
          ],
          'outputs': [
            '<(SHARED_INTERMEDIATE_DIR)/php-<@(libphp5_version)/configure'
          ],
          'action': ['python','./extract.py','./php-<@(libphp5_version).tar.gz','<(SHARED_INTERMEDIATE_DIR)', 'php-<@(libphp5_version)/configure']
        },
        {
          'action_name': 'configure_libphp5',
          'inputs': [
            '<(SHARED_INTERMEDIATE_DIR)/php-<@(libphp5_version)/configure'
          ],
          'outputs': [
            '<(SHARED_INTERMEDIATE_DIR)/php-<@(libphp5_version)/Makefile'
          ],
          'action': [
                './cdconfigure.js',
                '<(SHARED_INTERMEDIATE_DIR)/php-<@(libphp5_version)/',
                '--enable-maintainer-zts', '--enable-embed=static',
                '--prefix', '<(SHARED_INTERMEDIATE_DIR)/build',
                '--enable-opcache=static',
                # turn off some unnecessary bits
                '--disable-cli', '--disable-cgi',
                '<@(configure_options)'
          ]
        }
      ]
    },
    {
      'target_name': 'build',
      'dependencies': [
        'action_before_build'
      ],
      'actions': [
        {
          'action_name': 'build_libphp5',
          'inputs': [
            '<(SHARED_INTERMEDIATE_DIR)/php-<@(libphp5_version)/Makefile'
          ],
          'outputs': [
            '<(SHARED_INTERMEDIATE_DIR)/build/lib/libphp5.a'
          ],
          'action': ['make', '-C', '<(SHARED_INTERMEDIATE_DIR)/php-<@(libphp5_version)/',
                     '-j', '2', 'all', 'install']
        }
      ]
    },
    {
      'target_name': 'libphp5',
      'type': 'none',
      'dependencies': [
        'build'
      ],
      'direct_dependent_settings': {
        'include_dirs': [
          # these match `php-config --includes`
          '<(SHARED_INTERMEDIATE_DIR)/build/include/php',
          '<(SHARED_INTERMEDIATE_DIR)/build/include/php/main',
          '<(SHARED_INTERMEDIATE_DIR)/build/include/php/TSRM',
          '<(SHARED_INTERMEDIATE_DIR)/build/include/php/Zend',
          '<(SHARED_INTERMEDIATE_DIR)/build/include/php/ext',
          '<(SHARED_INTERMEDIATE_DIR)/build/include/php/ext/date/lib'
        ],
      },
      'link_settings': {
        'libraries': [
            '<(SHARED_INTERMEDIATE_DIR)/build/lib/libphp5.a',
            '<(SHARED_INTERMEDIATE_DIR)/php-<@(libphp5_version)/modules/opcache.a',
            # these match `php-config --libs`
            '<(libcrypt) -lresolv -lm -ldl -lxml2'
        ]
      },
      'sources': [
            '<(SHARED_INTERMEDIATE_DIR)/build/lib/libphp5.a'
      ]
    }
  ]
}
