/*
 * operation_wrapper.cpp  –  Node.js N-API wrapper for operation.c
 *
 *
 * Built by node-gyp via binding.gyp.
 */

#include <napi.h>

extern "C" {
#include "../src/include/operation.h"
}

using namespace Napi;

// --- operationInit() ---
Value operationInit(const CallbackInfo& info) {
    Env env = info.Env();
    operation_init();
    return env.Undefined();
}

// ---operationCleanup() ---
Value operationCleanup(const CallbackInfo& info) {
    Env env = info.Env();
    operation_cleanup();
    return env.Undefined();
}

// --- homingSequence() ---
Value homingSequence(const CallbackInfo& info) {
    Env env = info.Env();
    homing_sequence();
    return env.Undefined();
}

// --- tiltSignal(angle) ---
Value tiltSignal(const CallbackInfo& info) {
    Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsNumber()) {
        TypeError::New(env, "tiltSignal expects a number").ThrowAsJavaScriptException();
        return env.Null();
    }
    tilt_signal_advanced(info[0].As<Number>().FloatValue());
    return env.Undefined();
}

// --- speedSignal(speed) ---
Value speedSignal(const CallbackInfo& info) {
    Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsNumber()) {
        TypeError::New(env, "speedSignal expects a number").ThrowAsJavaScriptException();
        return env.Null();
    }
    speed_signal(info[0].As<Number>().FloatValue());
    return env.Undefined();
}

// --- resumeMachine() ---
Value resumeMachine(const CallbackInfo& info) {
    Env env = info.Env();
    resume_machine();
    return env.Undefined();
}

// --- pauseMachine() ---
Value pauseMachine(const CallbackInfo& info) {
    Env env = info.Env();
    pause_machine();
    return env.Undefined();
}

// --- hopperStart() ---
Value hopperStart(const CallbackInfo& info) {
    Env env = info.Env();
    hopper_start();
    return env.Undefined();
}

// --- hopperStop() ---
Value hopperStop(const CallbackInfo& info) {
    Env env = info.Env();
    hopper_stop();
    return env.Undefined();
}

// --- hopperPulse() ---
Value hopperPulse(const CallbackInfo& info) {
    Env env = info.Env();
    hopper_pulse();
    return env.Undefined();
}

// --- Module Init ---
Object Init(Env env, Object exports) {
    exports.Set("operationInit", Function::New(env, operationInit));
    exports.Set("operationCleanup", Function::New(env, operationCleanup));
    exports.Set("homingSequence", Function::New(env, homingSequence));
    exports.Set("tiltSignal", Function::New(env, tiltSignal));
    exports.Set("speedSignal", Function::New(env, speedSignal));
    exports.Set("resumeMachine", Function::New(env, resumeMachine));
    exports.Set("pauseMachine", Function::New(env, pauseMachine));
    exports.Set("hopperStart", Function::New(env, hopperStart));
    exports.Set("hopperStop", Function::New(env, hopperStop));
    exports.Set("hopperPulse", Function::New(env, hopperPulse));
    return exports;
}

NODE_API_MODULE(operation_wrapper, Init)