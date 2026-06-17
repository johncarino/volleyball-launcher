/*
 * motor_addon.cc  –  Node.js N-API wrapper for motor_control.c
 *
 * Exposes three JS functions:
 *   setSpeed(speed: number)   – 0-100 %
 *   setAngle(angle: number)   – 0-90 deg
 *   stopMotors()
 *
 * Built by node-gyp via binding.gyp.
 */

#include <napi.h>

extern "C" {
#include "../motor_control.h"
}

// ---- setSpeed(speed: number) -----------------------------------------------
Napi::Value SetSpeed(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "setSpeed expects a number").ThrowAsJavaScriptException();
        return env.Null();
    }
    set_motor_speed(info[0].As<Napi::Number>().Int32Value());
    return env.Undefined();
}

// ---- setAngle(angle: number) -----------------------------------------------
Napi::Value SetAngle(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "setAngle expects a number").ThrowAsJavaScriptException();
        return env.Null();
    }
    set_motor_angle(info[0].As<Napi::Number>().Int32Value());
    return env.Undefined();
}

// ---- stopMotors() ----------------------------------------------------------
Napi::Value StopMotors(const Napi::CallbackInfo& info) {
    stop_motor();
    return info.Env().Undefined();
}

// ---- Module init -----------------------------------------------------------
Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("setSpeed",    Napi::Function::New(env, SetSpeed));
    exports.Set("setAngle",    Napi::Function::New(env, SetAngle));
    exports.Set("stopMotors",  Napi::Function::New(env, StopMotors));
    return exports;
}

NODE_API_MODULE(motor_addon, Init)
