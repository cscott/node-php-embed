{
  'includes': [ 'common-libphp5.gypi' ],
  'variables': {
      'libicu%':'internal',
      'iculibs%':[],
  },
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
    'variables': {
      'configure_options': [],
    },
    'conditions': [
      ['OS == "win"', {
        'defines': [
          'WIN32'
        ],
      }],
      ['OS == "linux"', {
        'link_settings': {
          # these are libraries not needed on osx
          'libraries': [ '-lcrypt', '-lrt', '-lnsl' ],
        },
      }],
      ['OS == "mac"', {
        'link_settings': {
          'libraries': [ '-liconv' ],
        },
      }],
      ['libicu == "internal"', {
      }, 'OS == "mac"', {
        'variables': {
          'configure_options': ['--with-icu-dir=/usr/local/opt/icu4c/'],
        },
        'link_settings': {
          'ldflags': [ '-L/usr/local/opt/icu4c/lib' ],
          'libraries': [ '-L/usr/local/opt/icu4c/lib' ],
        }
      }],
    ],
  },

  'targets': [
    {
      'target_name': 'action_before_build',
      'type': 'none',
      'hard_dependency': 1,
      'conditions': [
        ['libicu == "internal"', {
          'dependencies': [
            'libicu.gyp:libicu'
          ],
          'export_dependent_settings': [
            'libicu.gyp:libicu'
          ],
          'variables': {
            'configure_options': ['--with-icu-dir=<(SHARED_INTERMEDIATE_DIR)/build'],
          },
        }]
      ],
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
          'action_name': 'unpack_apcu_dep',
          'inputs': [
            './apcu-<@(apcu_version).tgz'
          ],
          'outputs': [
            '<(SHARED_INTERMEDIATE_DIR)/php-<@(libphp5_version)/ext/apcu/config.m4'
          ],
          'action': ['python','./extract.py','./apcu-<@(apcu_version).tgz','<(SHARED_INTERMEDIATE_DIR)/php-<@(libphp5_version)/ext', 'apcu-<@(apcu_version)/config.m4', 'apcu-<@(apcu_version)', 'apcu']
        },
        {
          'action_name': 'configure_libphp5',
          'inputs': [
            '<(SHARED_INTERMEDIATE_DIR)/php-<@(libphp5_version)/configure',
            '<(SHARED_INTERMEDIATE_DIR)/php-<@(libphp5_version)/ext/apcu/config.m4'
          ],
          'outputs': [
            '<(SHARED_INTERMEDIATE_DIR)/php-<@(libphp5_version)/Makefile'
          ],
          'action': [
                './cdconfigure.js', '--rebuild',
                '<(SHARED_INTERMEDIATE_DIR)/php-<@(libphp5_version)/',
                '--enable-maintainer-zts', '--enable-embed=static',
                '--prefix', '<(SHARED_INTERMEDIATE_DIR)/build',
                # opcache can only be built shared
                '--enable-opcache=shared',
                # mediawiki says this is necessary
                '--with-zlib', '--enable-mbstring',
                # and this is recommended:
                '--enable-intl', '--enable-apcu',
                # turn off some unnecessary bits
                '--disable-cli', '--disable-cgi',
                '>@(configure_options)'
          ]
        }
      ]
    },
    {
      'target_name': 'build',
      'dependencies': [
        'action_before_build'
      ],
      'export_dependent_settings': [
        'action_before_build'
      ],
      'actions': [
        {
          'action_name': 'build_libphp5',
          'inputs': [
            '<(SHARED_INTERMEDIATE_DIR)/php-<@(libphp5_version)/Makefile'
          ],
          'outputs': [
            '<(SHARED_INTERMEDIATE_DIR)/build/lib/libphp5.a',
            #'<(SHARED_INTERMEDIATE_DIR)/php-<@(libphp5_version)/modules/opcache.so'
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
      'conditions': [
        ['libicu == "internal"', {
          'variables': {
            'iculibs': [
              '<(SHARED_INTERMEDIATE_DIR)/build/lib/libicui18n.a',
              '<(SHARED_INTERMEDIATE_DIR)/build/lib/libicuuc.a',
              '<(SHARED_INTERMEDIATE_DIR)/build/lib/libicudata.a',
              '<(SHARED_INTERMEDIATE_DIR)/build/lib/libicuio.a'
            ]
          }
        }, {
          'variables': {
            'iculibs': ['-licui18n','-licuuc','-licudata','-licuio']
          }
        }],
      ],
      'direct_dependent_settings': {
        'php_module_files': [
          '<(SHARED_INTERMEDIATE_DIR)/php-<@(libphp5_version)/modules/opcache.so',
        ],
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
        'ldflags': [],
        'libraries': [
            '<(SHARED_INTERMEDIATE_DIR)/build/lib/libphp5.a',
            # these match `php-config --libs`
            '-lresolv -lstdc++ -lz -lm -ldl -lxml2',
            # these match `icu-config --ldflags`, and are needed because
            # we are building the Intl extension.
            '<@(iculibs)'
        ]
      },
      'sources': [
            '<(SHARED_INTERMEDIATE_DIR)/build/lib/libphp5.a',
            #'<(SHARED_INTERMEDIATE_DIR)/php-<@(libphp5_version)/modules/opcache.so'
      ]
    }
  ]
}
