{
  "targets": [
    {
      "target_name": "motor_addon",
      "sources": [
        "motor_addon.cc",
        "../motor_control.c"
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")"
      ],
      "defines": [ "NAPI_DISABLE_CPP_EXCEPTIONS" ],
      "cflags_cc!": [ "-fno-exceptions" ]
    }
  ]
}
