{
  'includes': [ 'deps/common-libphp5.gypi' ],
  'variables': {
      'libphp5%':'internal',
  },
  'targets': [
    {
      'target_name': 'node_php_embed',
      "include_dirs": ["<!(node -e \"require('nan')\")"],
      'conditions': [
        ['libphp5 != "internal"', {
            'libraries': [ "<!@(php-config --ldflags) -lphp5" ],
            'cflags': [ "<!@(php-config --includes)" ]
        },
        {
            'dependencies': [
              'deps/libphp5.gyp:libphp5'
            ]
        }
        ]
      ],
      'xcode_settings': {
        'MACOSX_DEPLOYMENT_TARGET': '10.7',

        'OTHER_CFLAGS': [
          '-std=c++11',
          '-stdlib=libc++'
        ],
      },
      'sources': [
        'src/node_php_embed.cc',
        'src/node_php_jsobject_class.cc',
      ],
    },
    {
      "target_name": "action_after_build",
      "type": "none",
      "dependencies": [ "<(module_name)" ],
      "copies": [
        {
          "files": [ "<(PRODUCT_DIR)/<(module_name).node" ],
          "destination": "<(module_path)"
        }
      ]
    }
  ]
}
