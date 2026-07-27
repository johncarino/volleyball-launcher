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

extern set_specs_t set_seq[NUM_MACHINE_POSITIONS][NUM_SETS];

// --- setCalibration(netHeight, courtLength, courtWidth) ---
Value setCalibration(const CallbackInfo& info) {
    Env env = info.Env();
    if (info.Length() < 3 || !info[0].IsNumber() || !info[1].IsNumber() || !info[2].IsNumber()) {
        TypeError::New(env, "setCalibration expects three numbers").ThrowAsJavaScriptException();
        return env.Null();
    }

    float netHeight = info[0].As<Number>().FloatValue();
    float courtLength = info[1].As<Number>().FloatValue();
    float courtWidth = info[2].As<Number>().FloatValue();

    arc_calc_params(netHeight, courtWidth, courtLength);
    return env.Undefined();
}

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

    int setIndex = info[0].As<Number>().Int32Value();
    int machinePosition = info[1].As<Number>().Int32Value();

    int success = save_set(
        setIndex,
        machinePosition,
        info[2].As<Number>().Int32Value(),
        info[3].As<Number>().Int32Value()
    );

    // If save was successful, return the saved set data as a JS object
    if (success) {
        Object result = Object::New(env);
        set_specs_t& saved = set_seq[machinePosition][setIndex];
        
        result.Set("launch_speed", Number::New(env, saved.launch_speed));
        result.Set("tilt_angle", Number::New(env, saved.tilt_angle));
        result.Set("yaw_angle", Number::New(env, saved.yaw_angle));
        result.Set("rpm_output", Number::New(env, saved.rpm_output));
        result.Set("target_location", Number::New(env, saved.target_location));
        result.Set("tempo", Number::New(env, saved.tempo));
        
        return result;
    }
    
    return env.Null();
}

// --- recalculateSets() ---
// Re-derives every already-saved set slot from the latest calibration
// (call setCalibration() first so the tilt_angle/rpm_output/etc. tables are
// up to date). Returns an array of every currently-saved slot's data
// (including machine_position/set_index) so the caller can refresh any
// cache it keeps of the saved sets (e.g. gesture_server.js's savedSets).
Value recalculateSets(const CallbackInfo& info) {
    Env env = info.Env();
    recalculate_saved_sets();

    Array result = Array::New(env);
    uint32_t idx = 0;
    for (int mp = 0; mp < NUM_MACHINE_POSITIONS; mp++) {
        for (int set_index = 0; set_index < NUM_SETS; set_index++) {
            set_specs_t& slot = set_seq[mp][set_index];
            if (slot.target_location == 0) {
                continue; // never saved
            }

            Object entry = Object::New(env);
            entry.Set("machine_position", Number::New(env, mp));
            entry.Set("set_index", Number::New(env, set_index));
            entry.Set("launch_speed", Number::New(env, slot.launch_speed));
            entry.Set("tilt_angle", Number::New(env, slot.tilt_angle));
            entry.Set("yaw_angle", Number::New(env, slot.yaw_angle));
            entry.Set("rpm_output", Number::New(env, slot.rpm_output));
            entry.Set("target_location", Number::New(env, slot.target_location));
            entry.Set("tempo", Number::New(env, slot.tempo));
            result.Set(idx++, entry);
        }
    }

    return result;
}

// --- Module init ---
Object Init(Env env, Object exports) {
    exports.Set("setCalibration", Function::New(env, setCalibration));
    exports.Set("saveSet", Function::New(env, saveSet));
    exports.Set("recalculateSets", Function::New(env, recalculateSets));
    return exports;
}

NODE_API_MODULE(set_wrapper, Init)