{
  "targets": [{
    "target_name": "usb_addon",
    "cflags!": [ "-fno-exceptions" ],
    "cflags_cc!": [ "-fno-exceptions" ],
    "sources": [ 
      "src/usb_addon.cc"
    ],
    "include_dirs": [
      "<!@(node -p \"require('node-addon-api').include\")"
    ],
    'defines': [ 'NAPI_DISABLE_CPP_EXCEPTIONS' ],
    'conditions': [
      ['OS=="win"', {
        'libraries': [ 
          '-lsetupapi.lib',
          '-lwinusb.lib'
        ]
      }]
    ],
    "dependencies": [
      "<!(node -p \"require('node-addon-api').gyp\")"
    ],
    "libraries": [
      "setupapi.lib"
    ],
    "msvs_settings": {
      "VCCLCompilerTool": {
        "ExceptionHandling": 1
      }
    }
  }]
} 