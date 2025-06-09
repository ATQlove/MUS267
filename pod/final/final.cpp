#include "daisy_pod.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

DaisyPod    pod;
static float sampleRate;

//— DSP objects —//
// Kick drum: sine oscillator + percussive envelope
static Oscillator    kickOsc;
static Adsr          kickEnv;

// Snare drum: white noise → bandpass filter + percussive envelope
static WhiteNoise    snareNoise;
static Adsr          snareEnv;
static Svf           snareFilter;

// Metronome click: high-frequency sine + very short envelope
static Oscillator    clickOsc;
static Adsr          clickEnv;

// Hi-Hat: white noise → highpass filter + percussive envelope
static WhiteNoise    hiHatNoise;
static Adsr          hiHatEnv;
static Svf           hiHatFilter;

// Timing / state
static float         beatIntervalSamples = 0.0f;
static float         beatCounter         = 0.0f;

// Track button/encoder states for edge detection
static bool          lastButtonKick        = false;
static bool          lastButtonSnare       = false;
static bool          lastEncoderButton     = false;

// Drum-set parameters
constexpr int NUM_DRUM_SETS = 6;
static int   currentDrumSet = 0;

static const float hiHatParams[NUM_DRUM_SETS][2] = {
    // {filterFreqHz, decaySec}
    {12000.0f, 0.05f},   // Classic
    {10000.0f, 0.04f},   // Electronic
    { 8000.0f, 0.06f},   // 808 Style
    {11000.0f, 0.05f},   // Rock Kit
    { 9000.0f, 0.07f},   // Lo-Fi HipHop
    { 7000.0f, 0.03f}    // Industrial
};

// LED colors for each drum set (R, G, B)
static const uint8_t drumSetColors[NUM_DRUM_SETS][3] = {
    {255, 0, 0},    // Set 0: Red (Classic)
    {0, 255, 0},    // Set 1: Green (Electronic)
    {0, 0, 255},    // Set 2: Blue (808 Style)
    {255, 255, 0},  // Set 3: Yellow (Rock Kit)
    {255, 0, 255},  // Set 4: Magenta (Lo-Fi HipHop)
    {0, 255, 255}   // Set 5: Cyan (Industrial)
};

// Predefined parameters for each drum set:
//   [set][0] = kick frequency (Hz)
//   [set][1] = kick decay time (sec)
//   [set][2] = snare filter center-freq (Hz)
//   [set][3] = snare decay time (sec)
static const float drumParams[NUM_DRUM_SETS][4] = {
    {  60.0f, 0.20f, 1800.0f, 0.15f },  // Classic
    {  80.0f, 0.12f, 1200.0f, 0.10f },  // Electronic
    {  45.0f, 0.80f, 2200.0f, 0.10f },  // 808 Style
    {  55.0f, 0.28f, 2500.0f, 0.18f },  // Rock Kit
    {  70.0f, 0.15f, 1000.0f, 0.09f },  // Lo-Fi HipHop
    {  65.0f, 0.10f, 3500.0f, 0.12f }   // Industrial
};

// Map a normalized knob [0,1] to BPM in [60,180]
static inline float KnobToBPM(float k)    { return 60.0f + (180.0f - 60.0f) * k; }
// Map a normalized knob [0,1] to volume [0,1]
static inline float KnobToVolume(float k) { return k; }

// ==================== Global additions (placed after currentDrumSet definition) ====================
static bool  presetMode            = false;
static bool  presetPlaying         = false;
static int   presetStep            = 0;
static float subdivCounter         = 0.0f;
static float subdivIntervalSamples = 0.0f;

// Change PRESET_STEPS from 8 to 64
constexpr int PRESET_STEPS = 64;

// Four bars of 64-step preset rhythm
// B row: Bass drum (kick)
static const uint8_t presetBass[PRESET_STEPS] = {
    // bar1 (16 steps)
    1,0,1,0, 0,0,0,0, 0,0,1,1, 0,0,0,0,
    // bar2
    1,0,1,0, 0,0,0,0, 0,0,1,1, 0,0,0,0,
    // bar3
    1,0,1,0, 0,0,0,0, 0,0,1,0, 0,0,0,0,
    // bar4
    0,0,1,1, 0,0,0,0, 0,0,1,0, 0,0,0,0
};

// S row: Snare (retain original eighth-note rhythm, repeat on even indices)
static const uint8_t presetSnare[PRESET_STEPS] = {
    // bar1
    0,0,0,0, 1,0,0,1, 0,1,0,0, 1,0,0,1,
    // bar2
    0,0,0,0, 1,0,0,1, 0,1,0,0, 1,0,0,1,
    // bar3
    0,0,0,0, 1,0,0,1, 0,1,0,0, 0,0,1,0,
    // bar4
    0,1,0,0, 1,0,0,1, 0,1,0,0, 0,0,1,0
};

// R row: Hi-Hat (originally hit every 8th note, now hit on each pair of 16th notes)
static const uint8_t presetClick[PRESET_STEPS] = {
    // bar1–4: hit every two 16th notes
    1,0,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,0,
    1,0,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,0,
    1,0,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,0,
    1,0,1,0, 1,0,1,0, 1,0,0,0, 1,0,1,0
};

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        // 1) Read all control inputs each sample
        pod.ProcessAnalogControls();   // updates pod.GetKnobValue()
        pod.ProcessDigitalControls();  // updates pod.GetButton()

        // Read tempo & volume knobs
        float tempoKnob  = pod.GetKnobValue(DaisyPod::KNOB_1);   // Pot 1 → Tempo
        float volumeKnob = pod.GetKnobValue(DaisyPod::KNOB_2);   // Pot 2 → Volume

        // Convert to BPM & master volume
        float bpm    = KnobToBPM(tempoKnob);
        float volume = KnobToVolume(volumeKnob);

        // Update beat interval (samples per beat)
        beatIntervalSamples = sampleRate * (60.0f / bpm);

        // 2) Metronome: trigger click on each beat
        float clkSample = 0.0f;
        beatCounter += 1.0f;
        if (beatCounter >= beatIntervalSamples)
        {
            beatCounter -= beatIntervalSamples;
            clickEnv.Retrigger(false);
        }
        clkSample = clickOsc.Process() * clickEnv.Process(false);

        // Generate Hi-Hat noise sample
        float noiseSample = hiHatNoise.Process();
        hiHatFilter.Process(noiseSample);
        float hHatSample = hiHatFilter.High() * hiHatEnv.Process(false);

        // 3) Drum-set switching via encoder rotation
        int32_t encoderIncrement = pod.encoder.Increment();
        if (encoderIncrement != 0)
        {
            currentDrumSet = (currentDrumSet + encoderIncrement + NUM_DRUM_SETS) % NUM_DRUM_SETS;
            // Apply new parameters immediately
            kickOsc.SetFreq(       drumParams[currentDrumSet][0] );
            kickEnv.SetDecayTime(  drumParams[currentDrumSet][1] );
            snareFilter.SetFreq(   drumParams[currentDrumSet][2] );
            snareEnv.SetDecayTime( drumParams[currentDrumSet][3] );
            hiHatFilter.SetFreq(   hiHatParams[currentDrumSet][0] );
            hiHatEnv.SetDecayTime( hiHatParams[currentDrumSet][1] );

            // Update LED color for the current drum set
            pod.led1.Set(drumSetColors[currentDrumSet][0] / 255.0f,
                         drumSetColors[currentDrumSet][1] / 255.0f,
                         drumSetColors[currentDrumSet][2] / 255.0f);
            pod.led1.Update();
        }

        // Recalculate sample interval for 1/8 note
        subdivIntervalSamples = beatIntervalSamples * 0.25f;

        // New: 1/8 subdivision timing
        subdivCounter += 1.0f;
        bool newSubdivision = false;
        if (subdivCounter >= subdivIntervalSamples)
        {
            subdivCounter -= subdivIntervalSamples;
            newSubdivision = true;
        }

        // New: detect encoder button rising edge
        bool thisEncoderBtn = pod.encoder.Pressed();
        if (thisEncoderBtn && !lastEncoderButton)
        {
            presetMode = !presetMode;  // Toggle preset mode
            // Optional: indicate with LED color
            if (presetMode)
                pod.led1.Set(1.0f, 1.0f, 1.0f);  // White for preset mode
            else
                pod.led1.Set(drumSetColors[currentDrumSet][0] / 255.0f,
                             drumSetColors[currentDrumSet][1] / 255.0f,
                             drumSetColors[currentDrumSet][2] / 255.0f);
            pod.led1.Update();
        }
        lastEncoderButton = thisEncoderBtn;

        // New: handle buttons in preset mode
        bool thisKickBtn  = pod.button1.Pressed();
        bool thisSnareBtn = pod.button2.Pressed();

        if (!presetMode)
        {
            if (thisKickBtn  && !lastButtonKick)  kickEnv.Retrigger(false);
            if (thisSnareBtn && !lastButtonSnare) snareEnv.Retrigger(false);
        }
        else
        {
            // Preset mode: single click on Kick starts playback
            if (thisKickBtn && !lastButtonKick && !presetPlaying)
            {
                presetPlaying = true;
                presetStep    = 0;
                subdivCounter = 0.0f;
            }
        }
        lastButtonKick  = thisKickBtn;
        lastButtonSnare = thisSnareBtn;

        // New: trigger drum voices on each 1/8 subdivision in preset playback
        if (presetMode && presetPlaying && newSubdivision)
        {
            if (presetBass[presetStep])  kickEnv.Retrigger(false);
            if (presetSnare[presetStep]) snareEnv.Retrigger(false);
            if (presetClick[presetStep]) hiHatEnv.Retrigger(false);

            // Exit after playing 64 steps
            presetStep++;
            if (presetStep >= PRESET_STEPS)
            {
                presetPlaying = false;
                // (Optional) restore current drum set LED
                pod.led1.Set(drumSetColors[currentDrumSet][0] / 255.0f,
                             drumSetColors[currentDrumSet][1] / 255.0f,
                             drumSetColors[currentDrumSet][2] / 255.0f);
                pod.led1.Update();
            }
        }

        // 5) Generate kick sample
        float k = kickOsc.Process() * kickEnv.Process(false) * 2.0f;

        // 6) Generate snare sample
        float noise   = snareNoise.Process();
        snareFilter.Process(noise);
        float filtered = snareFilter.Band();
        float s       = filtered * snareEnv.Process(false);

        // 7) Mix kick + snare + click (and hi-hat in preset mode), apply volume
        float outSample;
        if (!presetMode)
        {
            outSample = (k + s + clkSample) * volume;
        }
        else
        {
            outSample = (k + s + clkSample + hHatSample) * volume;
        }

        // Mix generated audio with input pass-through
        float inputL = in[0][i];
        float inputR = in[1][i];

        out[0][i] = outSample + inputL;
        out[1][i] = outSample + inputR;
    }
}

int main(void)
{
    pod.Init();
    sampleRate = pod.AudioSampleRate();

    // Set initial LED color for the first drum set
    pod.led1.Set(drumSetColors[0][0] / 255.0f,
                 drumSetColors[0][1] / 255.0f,
                 drumSetColors[0][2] / 255.0f);
    pod.led1.Update();

    // Initialize encoder button state
    lastEncoderButton = pod.encoder.Pressed();

    // Kick drum initial setup (parameters from set 0)
    kickOsc.Init(sampleRate);
    kickOsc.SetWaveform(Oscillator::WAVE_SIN);
    kickOsc.SetFreq(       drumParams[0][0] );
    kickEnv.Init(sampleRate);
    kickEnv.SetAttackTime( 0.001f );  // 1 ms attack
    kickEnv.SetDecayTime(  drumParams[0][1] );
    kickEnv.SetSustainLevel(0.0f);

    // Snare drum initial setup (parameters from set 0)
    snareNoise.Init();
    snareEnv.Init(sampleRate);
    snareEnv.SetAttackTime( 0.001f );  // 1 ms attack
    snareEnv.SetDecayTime(  drumParams[0][3] );
    snareEnv.SetSustainLevel(0.0f);
    snareFilter.Init(sampleRate);
    snareFilter.SetFreq( drumParams[0][2] );
    snareFilter.SetRes(  0.7f );

    // Metronome click setup
    clickOsc.Init(sampleRate);
    clickOsc.SetWaveform(Oscillator::WAVE_SIN);
    clickOsc.SetFreq( 1000.0f );       // 1 kHz click
    clickEnv.Init(sampleRate);
    clickEnv.SetAttackTime(  0.0005f );// 0.5 ms attack
    clickEnv.SetDecayTime(   0.01f );  // 10 ms decay
    clickEnv.SetSustainLevel(0.0f);

    // Hi-Hat initial setup
    hiHatNoise.Init();
    hiHatEnv.Init(sampleRate);
    hiHatEnv.SetAttackTime(0.001f);    // 1 ms attack
    hiHatEnv.SetDecayTime(0.05f);      // Short decay
    hiHatEnv.SetSustainLevel(0.0f);
    hiHatFilter.Init(sampleRate);
    hiHatFilter.SetFreq(8000.0f);
    hiHatFilter.SetRes(0.7f);

    pod.StartAdc();       // enable knob/button scanning
    pod.StartAudio(AudioCallback);

    while (1) { }
}
