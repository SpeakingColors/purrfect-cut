// Purrfect Cut: the parameter table shared by the sampler engine, the plugin and the tests. Pure C++.
// Every control of the sampler and the FX chain is listed here once. The plugin turns each row into an automatable
// host parameter, so nothing in the sound can be changed without it being automatable.
#pragma once
#include <cstddef>

namespace purrfect {

enum P : int {
    // sampler: pitch and play
    sm_mode, sm_poly, sm_root, sm_tune, sm_fine, sm_bend, sm_glide,
    // sampler: sample
    sm_rs, sm_re, sm_start, sm_loop, sm_ls, sm_le, sm_rev, sm_smode, sm_slices,
    // sampler: amp, filter
    sm_a, sm_d, sm_s, sm_r, sm_gain, sm_vel, sm_pan, sm_ft, sm_fc, sm_fq, sm_fe, sm_fk,
    // FX chain
    dr_on, dr_d, dr_x, dr_f, dr_mix,
    sq_on, sq_depth, sq_mix,
    du_on, du_amt, du_shape, du_rate, du_bpm, du_hz, du_where, du_src, du_key,
    de_on, de_time, de_fb, de_shift, de_shim, de_damp, de_mix,
    rv_on, rv_type, rv_decay, rv_size, rv_pre, rv_mod, rv_damp, rv_shift, rv_shim, rv_mix,
    out_width, out_gain, fx_prev,
    kParamCount
};

enum class Kind { Float, Switch, Choice };

struct ParamSpec {
    const char* id; const char* name; const char* group; Kind kind;
    float min, max, def; float step; bool log; const char* choices;   // choices: names separated by '|'
};

// Choice indices
enum { MODE_KEYS = 0, MODE_SLICES = 1 };
enum { POLY = 0, MONO = 1 };
enum { SLICE_EVEN = 0, SLICE_TRANS = 1 };
enum { FT_OFF = 0, FT_LP = 1, FT_HP = 2, FT_BP = 3 };
enum { DUCK_DRY = 0, DUCK_TAILS = 1, DUCK_ALL = 2 };
enum { SRC_SHAPE = 0, SRC_SIDECHAIN = 1 };
enum { SH_PUMP, SH_SAW, SH_SWELL, SH_SINE, SH_TRI, SH_GATE, SH_HOLD, SH_STUTTER, SH_COUNT };

inline const ParamSpec* paramTable() {
    static const ParamSpec t[kParamCount] = {
        { "sm_mode", "Mode", "Pitch and play", Kind::Choice, 0, 1, 0, 1, false, "Keys|Slices" },
        { "sm_poly", "Voices", "Pitch and play", Kind::Choice, 0, 1, 0, 1, false, "Poly|Mono" },
        { "sm_root", "Root note", "Pitch and play", Kind::Float, 0, 127, 60, 1, false, nullptr },
        { "sm_tune", "Tune", "Pitch and play", Kind::Float, -24, 24, 0, 1, false, nullptr },
        { "sm_fine", "Fine tune", "Pitch and play", Kind::Float, -100, 100, 0, 1, false, nullptr },
        { "sm_bend", "Bend", "Pitch and play", Kind::Float, -12, 12, 0, 0.01f, false, nullptr },
        { "sm_glide", "Glide", "Pitch and play", Kind::Float, 0, 600, 0, 1, false, nullptr },
        { "sm_rs", "Region start", "Sample", Kind::Float, 0, 1, 0, 0.0005f, false, nullptr },
        { "sm_re", "Region end", "Sample", Kind::Float, 0, 1, 1, 0.0005f, false, nullptr },
        { "sm_start", "Start", "Sample", Kind::Float, 0, 1, 0, 0.0005f, false, nullptr },
        { "sm_loop", "Loop", "Sample", Kind::Switch, 0, 1, 0, 1, false, nullptr },
        { "sm_ls", "Loop start", "Sample", Kind::Float, 0, 1, 0, 0.0005f, false, nullptr },
        { "sm_le", "Loop end", "Sample", Kind::Float, 0, 1, 1, 0.0005f, false, nullptr },
        { "sm_rev", "Reverse", "Sample", Kind::Switch, 0, 1, 0, 1, false, nullptr },
        { "sm_smode", "Slice by", "Sample", Kind::Choice, 0, 1, 0, 1, false, "Even grid|Transients" },
        { "sm_slices", "Slices", "Sample", Kind::Float, 2, 24, 8, 1, false, nullptr },
        { "sm_a", "Attack", "Amp", Kind::Float, 0.5f, 2000, 2, 0, true, nullptr },
        { "sm_d", "Decay", "Amp", Kind::Float, 1, 4000, 300, 0, true, nullptr },
        { "sm_s", "Sustain", "Amp", Kind::Float, 0, 1, 1, 0.01f, false, nullptr },
        { "sm_r", "Release", "Amp", Kind::Float, 2, 5000, 120, 0, true, nullptr },
        { "sm_gain", "Gain", "Amp", Kind::Float, -24, 12, 0, 0.1f, false, nullptr },
        { "sm_vel", "Velocity", "Amp", Kind::Float, 0, 1, 0.6f, 0.01f, false, nullptr },
        { "sm_pan", "Pan", "Amp", Kind::Float, -1, 1, 0, 0.01f, false, nullptr },
        { "sm_ft", "Filter type", "Filter", Kind::Choice, 0, 3, 0, 1, false, "Off|Low pass|High pass|Band pass" },
        { "sm_fc", "Cutoff", "Filter", Kind::Float, 20, 20000, 18000, 0, true, nullptr },
        { "sm_fq", "Resonance", "Filter", Kind::Float, 0.5f, 12, 0.7f, 0.1f, false, nullptr },
        { "sm_fe", "Env amount", "Filter", Kind::Float, -1, 1, 0, 0.01f, false, nullptr },
        { "sm_fk", "Key track", "Filter", Kind::Float, 0, 1, 0, 0.01f, false, nullptr },
        { "dr_on", "Drive on", "Drive", Kind::Switch, 0, 1, 0, 1, false, nullptr },
        { "dr_d", "Drive", "Drive", Kind::Float, 0, 1, 0.3f, 0.01f, false, nullptr },
        { "dr_x", "Transistor", "Drive", Kind::Float, 0, 1, 0, 0.01f, false, nullptr },
        { "dr_f", "Fuzz", "Drive", Kind::Float, 0, 1, 0, 0.01f, false, nullptr },
        { "dr_mix", "Drive mix", "Drive", Kind::Float, 0, 1, 1, 0.01f, false, nullptr },
        { "sq_on", "Squash on", "Squash", Kind::Switch, 0, 1, 0, 1, false, nullptr },
        { "sq_depth", "Squash depth", "Squash", Kind::Float, 0, 1, 0.6f, 0.01f, false, nullptr },
        { "sq_mix", "Squash mix", "Squash", Kind::Float, 0, 1, 0.5f, 0.01f, false, nullptr },
        { "du_on", "Duck on", "Duck", Kind::Switch, 0, 1, 0, 1, false, nullptr },
        { "du_amt", "Duck amount", "Duck", Kind::Float, 0, 1, 0.8f, 0.01f, false, nullptr },
        { "du_shape", "Duck shape", "Duck", Kind::Choice, 0, 7, 0, 1, false, "Pump|Saw down|Swell|Sine|Triangle|Trance gate|Dip and hold|Stutter" },
        { "du_rate", "Duck rate", "Duck", Kind::Choice, 0, 5, 2, 1, false, "1/1|1/2|1/4|1/8|1/16|Free" },
        { "du_bpm", "Duck tempo", "Duck", Kind::Float, 60, 200, 128, 1, false, nullptr },
        { "du_hz", "Duck free Hz", "Duck", Kind::Float, 0.1f, 20, 2, 0, true, nullptr },
        { "du_where", "Duck target", "Duck", Kind::Choice, 0, 2, 0, 1, false, "Before echoes|Tails only|Everything" },
        { "du_src", "Duck source", "Duck", Kind::Choice, 0, 1, 0, 1, false, "Shape|Sidechain" },
        { "du_key", "Duck restart on key", "Duck", Kind::Switch, 0, 1, 1, 1, false, nullptr },
        { "de_on", "Delay on", "Delay", Kind::Switch, 0, 1, 0, 1, false, nullptr },
        { "de_time", "Delay time", "Delay", Kind::Float, 20, 2000, 375, 0, true, nullptr },
        { "de_fb", "Delay feedback", "Delay", Kind::Float, 0, 0.95f, 0.4f, 0.01f, false, nullptr },
        { "de_shift", "Delay shift", "Delay", Kind::Float, -12, 12, 0, 1, false, nullptr },
        { "de_shim", "Delay shimmer", "Delay", Kind::Float, 0, 1, 0, 0.01f, false, nullptr },
        { "de_damp", "Delay damping", "Delay", Kind::Float, 500, 16000, 5000, 0, true, nullptr },
        { "de_mix", "Delay mix", "Delay", Kind::Float, 0, 1, 0.3f, 0.01f, false, nullptr },
        { "rv_on", "Reverb on", "Reverb", Kind::Switch, 0, 1, 0, 1, false, nullptr },
        { "rv_type", "Reverb type", "Reverb", Kind::Choice, 0, 4, 0, 1, false, "Plate|Hall|Shimmer Plate|Shimmer Hall|Velvet" },
        { "rv_decay", "Reverb decay", "Reverb", Kind::Float, 0, 1, 0.6f, 0.01f, false, nullptr },
        { "rv_size", "Reverb size", "Reverb", Kind::Float, 0.5f, 1.6f, 1, 0.01f, false, nullptr },
        { "rv_pre", "Reverb pre-delay", "Reverb", Kind::Float, 0, 250, 20, 1, false, nullptr },
        { "rv_mod", "Reverb modulation", "Reverb", Kind::Float, 0, 1, 0.5f, 0.01f, false, nullptr },
        { "rv_damp", "Reverb damping", "Reverb", Kind::Float, 500, 16000, 6000, 0, true, nullptr },
        { "rv_shift", "Reverb shimmer pitch", "Reverb", Kind::Float, -12, 12, 12, 1, false, nullptr },
        { "rv_shim", "Reverb shimmer amount", "Reverb", Kind::Float, 0, 1, 0.5f, 0.01f, false, nullptr },
        { "rv_mix", "Reverb mix", "Reverb", Kind::Float, 0, 1, 0.3f, 0.01f, false, nullptr },
        { "out_width", "Width", "Output", Kind::Float, 0, 2, 1, 0.01f, false, nullptr },
        { "out_gain", "Output level", "Output", Kind::Float, -24, 12, 0, 0.1f, false, nullptr },
        { "fx_prev", "Sidechain input through FX", "Output", Kind::Switch, 0, 1, 0, 1, false, nullptr },
    };
    return t;
}

inline void defaultParams(float* v) { const ParamSpec* t = paramTable(); for (int i = 0; i < kParamCount; ++i) v[i] = t[i].def; }

}  // namespace purrfect
