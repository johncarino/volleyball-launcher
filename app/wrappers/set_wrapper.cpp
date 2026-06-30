/*
 * set_wrapper.cpp  –  Node.js N-API wrapper for set.c
 *
 *
 * Built by node-gyp via binding.gyp.
 */

#include <napi.h>

extern "C" {
#include "../src/include/set.h"
}

using namespace Napi;

// --- saveSet(set index, machine position, target location, tempo) ---
Value saveSet(const CallbackInfo& info) {
    Env env = info.Env();
    if (info.Length() < 4 || !info[0].IsNumber()) {
        TypeError::New(env, "setIndex expects a number").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!info[1].IsNumber()) {
        TypeError::New(env, "MachinePosition expects a number").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!info[2].IsNumber()) {
        TypeError::New(env,  "Target Location expects a number").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!info[3].IsNumber()) {
        TypeError::New(env,  "Tempo expects a number").ThrowAsJavaScriptException();
        return env.Null();
    }

    save_set(
        info[0].As<Number>().Int32Value(),
        info[1].As<Number>().Int32Value(),
        info[2].As<Number>().Int32Value(),
        info[3].As<Number>().Int32Value()
    );
    return env.Undefined();
}

// --- Module init ---
Object Init(Env env, Object exports) {
    exports.Set("saveSet", Function::New(env, saveSet));
    return exports;
}

NODE_API_MODULE(set_wrapper, Init)