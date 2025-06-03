// Drum Machine
// Knob 1 → Tempo (BPM 60–180)
// Knob 2 → Master Volume (0.0–1.0)
// Button 1 → Kick drum
// Button 2 → Snare drum
//
// Build and flash this to your Daisy Pod. Make sure you have daisy_pod and daisysp
// in your include path (as provided by the Daisy examples setup).

#include "daisy_pod.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

DaisyPod    pod;
static float sampleRate;

//— DSP objects —//
// Kick drum: sine oscillator + percussive envelope
static Oscillator    kickOsc;
static Adsr         kickEnv;

// Snare drum: white noise → bandpass filter + percussive envelope
static WhiteNoise    snareNoise;
static Adsr         snareEnv;
static Svf           snareFilter;

// Metronome click: high-freq sine + very short envelope
static Oscillator    clickOsc;
static Adsr         clickEnv;

// Timing / state
static float         beatIntervalSamples = 0.0f;
static float         beatCounter         = 0.0f;

// Helpers to track button rising edges
static bool          lastButtonStateKick  = false;
static bool          lastButtonStateSnare = false;

// Map a normalized knob [0,1] to BPM in [60,180]
static inline float  KnobToBPM(float k) { return 60.0f + (180.0f - 60.0f) * k; }
// Map a normalized knob [0,1] to volume [0, 1]
static inline float  KnobToVolume(float k) { return k; }

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    // Each callback: we process 'size' frames, stereo output
    for (size_t i = 0; i < size; i++)
    {
        // 1) Process control inputs at audio rate
        pod.ProcessAnalogControls();  // updates pod.GetKnobValue()
        pod.ProcessDigitalControls(); // updates pod.GetButton()

        // Read knobs (normalized 0.0 – 1.0)
        float tempoKnob   = pod.GetKnobValue(DaisyPod::KNOB_1);  // Pot 1 → Tempo
        float volumeKnob  = pod.GetKnobValue(DaisyPod::KNOB_2);  // Pot 2 → Volume

        // Convert to BPM & volume
        float bpm     = KnobToBPM(tempoKnob);
        float volume  = KnobToVolume(volumeKnob);

        // Update beat interval (samples per beat)
        beatIntervalSamples = sampleRate * (60.0f / bpm);

        // 2) Check metronome beat
        float clkSample = 0.0f;
        beatCounter += 1.0f;
        if (beatCounter >= beatIntervalSamples)
        {
            beatCounter -= beatIntervalSamples;
            clickEnv.Retrigger(false);  // fire a click
        }
        // Process click oscillator + envelope
        clkSample = clickOsc.Process() * clickEnv.Process(false);

        // 3) Check button presses (rising edge detection)
        bool thisButtonKick  = pod.button1.Pressed(); // Button 1 triggers kick
        bool thisButtonSnare = pod.button2.Pressed(); // Button 2 triggers snare

        // Kick button rising edge
        if (thisButtonKick && !lastButtonStateKick)
            kickEnv.Retrigger(false);
        lastButtonStateKick = thisButtonKick;

        // Snare button rising edge
        if (thisButtonSnare && !lastButtonStateSnare)
            snareEnv.Retrigger(false);
        lastButtonStateSnare = thisButtonSnare;

        // 4) Generate kick sample
        float k = kickOsc.Process() * kickEnv.Process(false);

        // 5) Generate snare sample
        float n          = snareNoise.Process();
        snareFilter.Process(n);
        float filtered   = snareFilter.Band();  // Use bandpass output
        float s          = filtered * snareEnv.Process(false);

        // 6) Mix everything and apply master volume
        float outSample  = (k + s + clkSample) * volume;

        // Write to both left/right
        out[0][i] = outSample;
        out[1][i] = outSample;
    }
}

int main(void)
{
    // 1) Initialize hardware
    pod.Init();
    sampleRate = pod.AudioSampleRate();

    // 2) Initialize DSP modules

    // --- Kick drum setup ---
    // 60 Hz sine, percussive envelope (fast attack, moderate decay)
    kickOsc.Init(sampleRate);
    kickOsc.SetWaveform(Oscillator::WAVE_SIN);
    kickOsc.SetFreq(60.0f);
    kickEnv.Init(sampleRate);
    kickEnv.SetAttackTime(0.001f);   // 1 ms attack
    kickEnv.SetDecayTime(0.2f);     // 200 ms decay
    kickEnv.SetSustainLevel(0.0f);

    // --- Snare drum setup ---
    snareNoise.Init();  // white noise
    snareEnv.Init(sampleRate);
    snareEnv.SetAttackTime(0.001f);  // 1 ms attack
    snareEnv.SetDecayTime(0.15f);   // 150 ms decay
    snareEnv.SetSustainLevel(0.0f);
    snareFilter.Init(sampleRate);
    snareFilter.SetFreq(1800.0f); // center freq ~ 1.8 kHz
    snareFilter.SetRes(0.7f);     // moderate resonance

    // --- Metronome click setup ---
    clickOsc.Init(sampleRate);
    clickOsc.SetWaveform(Oscillator::WAVE_SIN);
    clickOsc.SetFreq(1000.0f);    // 1 kHz click
    clickEnv.Init(sampleRate);
    clickEnv.SetAttackTime(0.0005f); // 0.5 ms attack
    clickEnv.SetDecayTime(0.01f);   // 10 ms decay
    clickEnv.SetSustainLevel(0.0f);

    // 3) Start audio callback at 48kHz, stereo, 16 frames per buffer
    pod.StartAdc();  // enable knob/button reading
    pod.StartAudio(AudioCallback);

    // 4) Keep running
    while (1) { /* Nothing here; audio runs in interrupt */}
}
