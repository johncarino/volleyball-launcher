/*
 * operation_wrapper.cpp  –  Node.js N-API wrapper for operation.c
 *
 *
 * Built by node-gyp via binding.gyp.
 */

#include <napi.h>
#include <iostream>

extern "C" {
#include "../src/include/operation.h"
}

using namespace Napi;

// --- operationInit() ---
Value operationInit(const CallbackInfo& info) {
    Env env = info.Env();
    std::cerr << "[operation] operationInit() entered" << std::endl;
    operation_init();
    std::cerr << "[operation] control layer initialized" << std::endl;
    
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
    tilt_with_feedback(info[0].As<Number>().FloatValue());
    std::cout << "[operation] tilt signal sent: " << info[0].As<Number>().FloatValue() << std::endl;
    return env.Undefined();
}

// --- speedSignal(speed) ---
Value speedSignal(const CallbackInfo& info) {
    Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsNumber()) {
        TypeError::New(env, "speedSignal expects a number").ThrowAsJavaScriptException();
        return env.Null();
    }
    percentage_to_mv(info[0].As<Number>().FloatValue());
    std::cout << "[operation] speed signal sent: " << info[0].As<Number>().FloatValue() << std::endl;
    return env.Undefined();
}

// --- syncSet(machinePosition, setIndex, launchSpeed, tiltAngle, yawAngle, rpmOutput, targetLocation, tempo) ---
Value syncSet(const CallbackInfo& info) {
    Env env = info.Env();
    if (info.Length() < 8 || !info[0].IsNumber() || !info[1].IsNumber() || !info[2].IsNumber() ||
        !info[3].IsNumber() || !info[4].IsNumber() || !info[5].IsNumber() ||
        !info[6].IsNumber() || !info[7].IsNumber()) {
        TypeError::New(env, "syncSet expects 8 numbers").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    int machine_position = info[0].As<Number>().Int32Value();
    int set_index = info[1].As<Number>().Int32Value();
    float launch_speed = info[2].As<Number>().FloatValue();
    float tilt_angle = info[3].As<Number>().FloatValue();
    float yaw_angle = info[4].As<Number>().FloatValue();
    float rpm_output = info[5].As<Number>().FloatValue();
    int target_location = info[6].As<Number>().Int32Value();
    int tempo = info[7].As<Number>().Int32Value();
    
    // Store into operation_wrapper's set_seq
    extern set_specs_t set_seq[NUM_MACHINE_POSITIONS][NUM_SETS];
    set_seq[machine_position][set_index].launch_speed = launch_speed;
    set_seq[machine_position][set_index].tilt_angle = tilt_angle;
    set_seq[machine_position][set_index].yaw_angle = yaw_angle;
    set_seq[machine_position][set_index].rpm_output = rpm_output;
    set_seq[machine_position][set_index].target_location = target_location;
    set_seq[machine_position][set_index].tempo = tempo;
    
    std::cout << "[operation] synced set " << set_index << " for machine " << machine_position 
              << ": tilt=" << tilt_angle << "°, rpm=" << rpm_output << std::endl;
    
    return env.Undefined();
}

Value setMachine(const CallbackInfo& info) {
    Env env = info.Env();
    if (info.Length() < 2 || !info[0].IsNumber() || !info[1].IsNumber()) {
        TypeError::New(env, "setMachine expects two numbers").ThrowAsJavaScriptException();
        return env.Null();
    }
    int machine_position = info[0].As<Number>().Int32Value();
    int set_index = info[1].As<Number>().Int32Value();
    set_machine(machine_position, set_index);
    std::cout << "[operation] machine set for position: " << machine_position << ", set index: " << set_index << std::endl;
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
    exports.Set("syncSet", Function::New(env, syncSet));
    exports.Set("setMachine", Function::New(env, setMachine));
    exports.Set("resumeMachine", Function::New(env, resumeMachine));
    exports.Set("pauseMachine", Function::New(env, pauseMachine));
    exports.Set("hopperStart", Function::New(env, hopperStart));
    exports.Set("hopperStop", Function::New(env, hopperStop));
    exports.Set("hopperPulse", Function::New(env, hopperPulse));
    return exports;
}

NODE_API_MODULE(operation_wrapper, Init)