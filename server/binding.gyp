{
  "targets": [
    {
      "target_name": "calibration_wrapper",
      "sources": [
        "../app/wrappers/calibration_wrapper.cpp",
        "../app/src/calibration.c",
        "../app/src/arc_calc.c"
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "..",
        "../app/src/include"
      ],
      "defines": [ "NAPI_DISABLE_CPP_EXCEPTIONS" ],
      "cflags_cc!": [ "-fno-exceptions" ],
      "libraries": [
        "-lm"
      ]
    },
    {
      "target_name": "set_wrapper",
      "sources": [
        "../app/wrappers/set_wrapper.cpp",
        "../app/src/set.c",
        "../app/src/arc_calc.c"
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "..",
        "../app/src/include"
      ],
      "defines": [ "NAPI_DISABLE_CPP_EXCEPTIONS" ],
      "cflags_cc!": [ "-fno-exceptions" ],
      "libraries": [
        "-lm"
      ]
    },
    {
      "target_name": "operation_wrapper",
      "sources": [
        "../app/wrappers/operation_wrapper.cpp",
        "../app/src/operation.c",
        "../app/src/set.c",
        "../app/src/arc_calc.c",
        "../hal/src/bts7960.c",
        "../hal/src/mcp4725.c",
        "../hal/src/mpu6050.c",
        "../hal/src/tb6600.c",
        "../hal/src/pwm.c"
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "..",
        "../app/src/include",
        "..",
        "../hal/include"
      ],
      "defines": [ "NAPI_DISABLE_CPP_EXCEPTIONS" ],
      "cflags_cc!": [ "-fno-exceptions" ],
      "libraries": [
        "-lm",
        "-lgpiod"
      ]
    }
  ]
}
