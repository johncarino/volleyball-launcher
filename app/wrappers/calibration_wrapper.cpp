/*
 * calibration_wrapper.cpp  –  Node.js N-API wrapper for calibration.c
 *
 *
 * Built by node-gyp via binding.gyp.
 */

#include <napi.h>

extern "C" {
#include "../src/include/calibration.h"
}

using namespace Napi;

// --- setNetHeight(height) ---
Value setNetHeight(const CallbackInfo& info) {
    Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsNumber()) {
        TypeError::New(env, "setNetHeight expects a number").ThrowAsJavaScriptException();
        return env.Null();
    }
    set_net_height(info[0].As<Number>().FloatValue());
    return env.Undefined();
}

// --- setCourtDimensions(length, width) ---
Value setCourtDimensions(const CallbackInfo& info) {
    Env env = info.Env();
    if (info.Length() < 2 || !info[0].IsNumber() || !info[1].IsNumber()) {
        TypeError::New(env, "setCourtDimensions expects two numbers").ThrowAsJavaScriptException();
        return env.Null();
    }
    set_court_dimensions(info[0].As<Number>().FloatValue(), info[1].As<Number>().FloatValue());
    return env.Undefined();
}

// --- setCourtLength(length) ---
Value setCourtLength(const CallbackInfo& info) {
    Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsNumber()) {
        TypeError::New(env, "setCourtLength expects a number").ThrowAsJavaScriptException();
        return env.Null();
    }
    set_court_length(info[0].As<Number>().FloatValue());
    return env.Undefined();
}

// --- setCourtWidth(width) ---
Value setCourtWidth(const CallbackInfo& info) {
    Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsNumber()) {
        TypeError::New(env, "setCourtWidth expects a number").ThrowAsJavaScriptException();
        return env.Null();
    }
    set_court_width(info[0].As<Number>().FloatValue());
    return env.Undefined();
}

// --- Module Init ---
Object Init(Env env, Object exports) {
    exports.Set("setNetHeight", Function::New(env, setNetHeight));
    exports.Set("setCourtDimensions", Function::New(env, setCourtDimensions));
    exports.Set("setCourtLength", Function::New(env, setCourtLength));
    exports.Set("setCourtWidth", Function::New(env, setCourtWidth));
    return exports;
}

NODE_API_MODULE(calibration_wrapper, Init)