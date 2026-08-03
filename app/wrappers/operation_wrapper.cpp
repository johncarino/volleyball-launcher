/*
 * operation_wrapper.cpp  –  Node.js N-API wrapper for operation.c
 *
 *
 * Built by node-gyp via binding.gyp.
 */

#include <napi.h>
#include <iostream>
#include <atomic>

extern "C" {
#include "../src/include/operation.h"
}

using namespace Napi;

// Guards against overlapping tilt operations. tilt_signal() mutates shared
// hardware state (DAC, motor driver, curr_tilt_angle), so only one tilt may
// run at a time. Set true when a TiltWorker is queued, cleared when it finishes.
static std::atomic<bool> g_tiltInProgress(false);
static std::atomic<bool> g_homingInProgress(false);

// Runs the blocking tilt_signal() feedback loop on a libuv worker thread so it
// no longer freezes Node's event loop (telemetry and other socket handlers
// keep running while the actuator moves).
class TiltWorker : public AsyncWorker {
public:
    TiltWorker(const Napi::Function& callback, float angle)
        : AsyncWorker(callback, "TiltWorker"), angle_(angle) {}

    ~TiltWorker() override {}

    // Executed on a worker thread -- safe to block here.
    void Execute() override {
        operation_clear_feedback_fault();
        tilt_signal(angle_);
        const char* fault = operation_feedback_fault_message();
        if (fault != nullptr) SetError(fault);
    }

    // Back on the main thread once the tilt returns.
    void OnOK() override {
        std::cout << "[operation] tilt signal complete: " << angle_ << std::endl;
        g_tiltInProgress.store(false);
        Callback().Call({Env().Null(), Boolean::New(Env(), true)});
    }

    void OnError(const Error& e) override {
        std::cerr << "[operation] tilt worker error: " << e.Message() << std::endl;
        g_tiltInProgress.store(false);
        Callback().Call({Error::New(Env(), e.Message()).Value(),
                         Boolean::New(Env(), false)});
    }

private:
    float angle_;
};

// Runs the blocking set_machine() call (which itself drives the tilt feedback
// loop, see operation.c) on a libuv worker thread. set_machine() is invoked
// once per selected set slot in both Single Shot and Sequence launch modes,
// so keeping it off the main thread is what keeps telemetry/sockets
// responsive while the machine re-tilts between shots.
class SetMachineWorker : public AsyncWorker {
public:
    SetMachineWorker(const Napi::Function& callback, int machinePosition, int setIndex)
        : AsyncWorker(callback, "SetMachineWorker"),
          machinePosition_(machinePosition), setIndex_(setIndex) {}

    ~SetMachineWorker() override {}

    void Execute() override {
        operation_clear_feedback_fault();
        set_machine(machinePosition_, setIndex_);
        const char* fault = operation_feedback_fault_message();
        if (fault != nullptr) SetError(fault);
    }

    void OnOK() override {
        std::cout << "[operation] set_machine complete: position=" << machinePosition_
                  << ", set=" << setIndex_ << std::endl;
        g_tiltInProgress.store(false);
        Callback().Call({Env().Null(), Boolean::New(Env(), true)});
    }

    void OnError(const Error& e) override {
        std::cerr << "[operation] setMachine worker error: " << e.Message() << std::endl;
        g_tiltInProgress.store(false);
        Callback().Call({Error::New(Env(), e.Message()).Value(), Boolean::New(Env(), false)});
    }

private:
    int machinePosition_;
    int setIndex_;
};

class HomingWorker : public AsyncWorker {
public:
    HomingWorker(const Napi::Function& callback)
        : AsyncWorker(callback, "HomingWorker") {}

    void Execute() override {
        homing_sequence();
    }

    void OnOK() override {
        g_homingInProgress.store(false);
        Callback().Call({Env().Null(), Boolean::New(Env(), true)});
    }

    void OnError(const Error& e) override {
        g_homingInProgress.store(false);
        Callback().Call({Error::New(Env(), e.Message()).Value(),
                         Boolean::New(Env(), false)});
    }
};

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
    if (info.Length() < 1 || !info[0].IsFunction()) {
        TypeError::New(env, "homingSequence expects a callback")
            .ThrowAsJavaScriptException();
        return env.Null();
    }
    bool expected = false;
    if (!g_homingInProgress.compare_exchange_strong(expected, true)) {
        return Boolean::New(env, false);
    }
    (new HomingWorker(info[0].As<Function>()))->Queue();
    return Boolean::New(env, true);
}

// --- tiltSignal(angle) ---
// Non-blocking: queues the tilt feedback loop on a worker thread and returns
// immediately so the Node event loop (telemetry, sockets) stays responsive.
Value tiltSignal(const CallbackInfo& info) {
    Env env = info.Env();
    if (info.Length() < 2 || !info[0].IsNumber() || !info[1].IsFunction()) {
        TypeError::New(env, "tiltSignal expects a number and callback").ThrowAsJavaScriptException();
        return env.Null();
    }
    float angle = info[0].As<Number>().FloatValue();

    bool expected = false;
    if (!g_tiltInProgress.compare_exchange_strong(expected, true)) {
        std::cout << "[operation] tiltSignal ignored (angle=" << angle
                  << "): a tilt is already in progress." << std::endl;
        return Boolean::New(env, false);
    }

    std::cout << "[operation] tiltSignal received (angle=" << angle
              << "); running tilt asynchronously..." << std::endl;

    TiltWorker* worker = new TiltWorker(info[1].As<Function>(), angle);
    worker->Queue();
    return Boolean::New(env, true);
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
    if (info.Length() < 3 || !info[0].IsNumber() || !info[1].IsNumber() || !info[2].IsFunction()) {
        TypeError::New(env, "setMachine expects two numbers and a callback").ThrowAsJavaScriptException();
        return env.Null();
    }
    int machine_position = info[0].As<Number>().Int32Value();
    int set_index = info[1].As<Number>().Int32Value();

    // set_machine() ends up calling tilt_signal() internally, which mutates
    // the same shared hardware state (DAC, motor driver, curr_tilt_angle) as
    // the standalone tiltSignal() above, so it shares the same in-flight
    // guard rather than blocking the event loop.
    bool expected = false;
    if (!g_tiltInProgress.compare_exchange_strong(expected, true)) {
        std::cout << "[operation] setMachine ignored (position=" << machine_position
                  << ", set=" << set_index << "): a tilt is already in progress." << std::endl;
        return Boolean::New(env, false);
    }

    std::cout << "[operation] setMachine received (position=" << machine_position
              << ", set=" << set_index << "); running asynchronously..." << std::endl;

    SetMachineWorker* worker = new SetMachineWorker(
        info[2].As<Function>(), machine_position, set_index);
    worker->Queue();
    return Boolean::New(env, true);
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
// Returns true if the hopper actually started, false if it was refused (e.g.
// the launcher isn't running -- see hopper_start()'s gating in operation.c).
Value hopperStart(const CallbackInfo& info) {
    Env env = info.Env();
    int rc = hopper_start();
    return Boolean::New(env, rc == 0);
}

// --- hopperStop() ---
Value hopperStop(const CallbackInfo& info) {
    Env env = info.Env();
    hopper_stop();
    return env.Undefined();
}

// --- hopperPulse() ---
// Returns true if the pulse actually ran, false if it was refused.
Value hopperPulse(const CallbackInfo& info) {
    Env env = info.Env();
    int rc = hopper_pulse();
    return Boolean::New(env, rc == 0);
}

// --- getTachReading() ---
Value getTachReading(const CallbackInfo& info) {
    Env env = info.Env();
    int rpm = get_tach_reading();
    return Number::New(env, rpm);
}

// --- getHopperPulseCount() ---
// Authoritative hopper pulse counter used by the UI's "shots until re-home".
Value getHopperPulseCount(const CallbackInfo& info) {
    Env env = info.Env();
    return Number::New(env, get_hopper_pulse_count());
}

// --- getComponentStatus() ---
// Bitmask of which HAL components initialized successfully (COMPONENT_* bits).
Value getComponentStatus(const CallbackInfo& info) {
    Env env = info.Env();
    return Number::New(env, operation_component_status());
}

// --- requestInterrupt() ---
// Raises the software interrupt (emergency abort), asking any in-progress
// blocking operation (tilt/speed feedback loops, hopper stepping, etc.)
// to abort and leave the motors stopped.
Value requestInterrupt(const CallbackInfo& info) {
    Env env = info.Env();
    operation_request_interrupt();
    std::cerr << "[operation] software interrupt requested" << std::endl;
    return env.Undefined();
}

Value markHopperMisaligned(const CallbackInfo& info) {
    Env env = info.Env();
    hopper_mark_misaligned();
    return env.Undefined();
}

Value hopperNeedsHoming(const CallbackInfo& info) {
    Env env = info.Env();
    return Boolean::New(env, hopper_needs_homing() != 0);
}

Value forceStop(const CallbackInfo& info) {
    Env env = info.Env();
    operation_force_stop();
    return env.Undefined();
}

// --- isInterruptPending() ---
Value isInterruptPending(const CallbackInfo& info) {
    Env env = info.Env();
    return Boolean::New(env, operation_interrupt_pending() != 0);
}

// --- clearInterrupt() ---
Value clearInterrupt(const CallbackInfo& info) {
    Env env = info.Env();
    operation_clear_interrupt();
    std::cerr << "[operation] software interrupt cleared" << std::endl;
    return env.Undefined();
}

// --- Module Init ---
Object Init(Env env, Object exports) {
    exports.Set("operationInit", Function::New(env, operationInit));
    exports.Set("operationCleanup", Function::New(env, operationCleanup));
    exports.Set("homingSequence", Function::New(env, homingSequence));
    exports.Set("tiltSignal", Function::New(env, tiltSignal));
    exports.Set("speedSignal", Function::New(env, speedSignal));
    exports.Set("getTachReading", Function::New(env, getTachReading));
    exports.Set("getHopperPulseCount", Function::New(env, getHopperPulseCount));
    exports.Set("markHopperMisaligned", Function::New(env, markHopperMisaligned));
    exports.Set("hopperNeedsHoming", Function::New(env, hopperNeedsHoming));
    exports.Set("getComponentStatus", Function::New(env, getComponentStatus));
    exports.Set("syncSet", Function::New(env, syncSet));
    exports.Set("setMachine", Function::New(env, setMachine));
    exports.Set("resumeMachine", Function::New(env, resumeMachine));
    exports.Set("pauseMachine", Function::New(env, pauseMachine));
    exports.Set("hopperStart", Function::New(env, hopperStart));
    exports.Set("hopperStop", Function::New(env, hopperStop));
    exports.Set("hopperPulse", Function::New(env, hopperPulse));
    exports.Set("requestInterrupt", Function::New(env, requestInterrupt));
    exports.Set("forceStop", Function::New(env, forceStop));
    exports.Set("isInterruptPending", Function::New(env, isInterruptPending));
    exports.Set("clearInterrupt", Function::New(env, clearInterrupt));
    return exports;
}

NODE_API_MODULE(operation_wrapper, Init)
