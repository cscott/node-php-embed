{
  'includes': [ 'common-libicu.gypi' ],
  'target_defaults': {
    'default_configuration': 'Release',
    'configurations': {
      'Debug': {
        'variables': {
          'libicu_configure_options': ['--enable-debug']
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
          'libicu_configure_options': ['--enable-release']
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
      'libicu_configure_options': [],
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
      'target_name': 'libicu_action_before_build',
      'type': 'none',
      'hard_dependency': 1,
      'actions': [
        {
          'action_name': 'unpack_libicu_dep',
          'inputs': [
            './icu4c-<@(libicu_version)-src.tgz'
          ],
          'outputs': [
            '<(SHARED_INTERMEDIATE_DIR)/icu/source/configure'
          ],
          'action': ['python','./extract.py','./icu4c-<@(libicu_version)-src.tgz','<(SHARED_INTERMEDIATE_DIR)', 'icu/source/configure']
        },
        {
          'action_name': 'configure_libicu',
          'inputs': [
            '<(SHARED_INTERMEDIATE_DIR)/icu/source/configure'
          ],
          'outputs': [
            '<(SHARED_INTERMEDIATE_DIR)/icu/source/Makefile'
          ],
          'action': [
                './cdconfigure.js',
                '<(SHARED_INTERMEDIATE_DIR)/icu/source/',
                '--prefix', '<(SHARED_INTERMEDIATE_DIR)/build',
                '--enable-static', '--disable-shared',
                # we can't use --disable-tools since the build process
                # uses bin/icupkg internally
                '--disable-layout', '--disable-extras',
                '--disable-tests', '--disable-samples',
                #'<@(libicu_configure_options)',
                '--with-data-packaging=static'
          ]
        }
      ]
    },
    {
      'target_name': 'libicu_build',
      'dependencies': [
        'libicu_action_before_build'
      ],
      'actions': [
        {
          'action_name': 'build_libicu',
          'inputs': [
            '<(SHARED_INTERMEDIATE_DIR)/icu/source/Makefile'
          ],
          'outputs': [
            '<(SHARED_INTERMEDIATE_DIR)/build/lib/libicudata.a',
            '<(SHARED_INTERMEDIATE_DIR)/build/lib/libicui18n.a',
            '<(SHARED_INTERMEDIATE_DIR)/build/lib/libicuio.a',
            '<(SHARED_INTERMEDIATE_DIR)/build/lib/libicuuc.a'
          ],
          'action': ['make', '-C', '<(SHARED_INTERMEDIATE_DIR)/icu/source/',
                     'all', 'install']
        }
      ]
    },
    {
      'target_name': 'libicu',
      'type': 'none',
      'dependencies': [
        'libicu_build'
      ],
      'sources': [
            '<(SHARED_INTERMEDIATE_DIR)/build/lib/libicudata.a',
            '<(SHARED_INTERMEDIATE_DIR)/build/lib/libicui18n.a',
            '<(SHARED_INTERMEDIATE_DIR)/build/lib/libicuio.a',
            '<(SHARED_INTERMEDIATE_DIR)/build/lib/libicuuc.a',
      ]
    }
  ]
}
