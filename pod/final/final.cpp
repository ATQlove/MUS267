// Drum Machine for Daisy Pod with Drum-Set Switching via Big Encoder
//
// Knob 1  → Tempo (BPM 60–180)
// Knob 2  → Master Volume (0.0–1.0)
// Button 1→ Kick drum (current drum set's kick)
// Button 2→ Snare drum (current drum set's snare)
// Encoder (big knob) rotation & push → Switch between drum sets
//
// This example assumes the DaisyPod API exposes:
//   KNOB_0, KNOB_1, KNOB_LAST   (where KNOB_LAST is the big encoder's index)
//   BUTTON_0, BUTTON_1, BUTTON_LAST (BUTTON_LAST is the encoder-push button)
//
// Each time you press the encoder (BUTTON_LAST), it steps to the next drum set.
// You can define as many drum sets as you like; here we show two sets for illustration.

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

// Track button/encoder states for edge detection
static bool          lastButtonKick        = false;
static bool          lastButtonSnare       = false;
static bool          lastEncoderButton     = false;

// Drum-set parameters
constexpr int NUM_DRUM_SETS = 6;
static int   currentDrumSet = 0;

// Predefined parameters for each drum set:
//   [set][0] = kick frequency (Hz)
//   [set][1] = kick decay time (sec)
//   [set][2] = snare filter center-freq (Hz)
//   [set][3] = snare decay time (sec)
static const float drumParams[NUM_DRUM_SETS][4] = {
    // Set 0: "Classic" drum set
    {  60.0f, 0.20f, 1800.0f, 0.15f },

    // Set 1: "Electronic" drum set
    {  80.0f, 0.12f, 1200.0f, 0.10f },

    // Set 2: "808 Style" (inspired by TR-808)
    // Characteristics: Deep and powerful kick with longer decay; crisp snare with shorter decay
    {  45.0f, 0.80f, 2200.0f, 0.10f },

    // Set 3: "Rock Kit" (more punchy and acoustic)
    // Characteristics: Solid kick; bright and punchy snare
    {  55.0f, 0.28f, 2500.0f, 0.18f },

    // Set 4: "Lo-Fi HipHop" (soft, slightly retro)
    // Characteristics: Kick with slightly higher frequency and short decay; 
    // Snare with lower frequency for a "warmer" or "muffled" sound
    {  70.0f, 0.15f, 1000.0f, 0.09f },

    // Set 5: "Industrial" (hard, aggressive)
    // Characteristics: Very short and powerful kick; 
    // High-frequency snare with harsh noise characteristics
    {  65.0f, 0.10f, 3500.0f, 0.12f }
};

// Map a normalized knob [0,1] to BPM in [60,180]
static inline float KnobToBPM(float k) { return 60.0f + (180.0f - 60.0f) * k; }
// Map a normalized knob [0,1] to volume [0, 1]
static inline float KnobToVolume(float k) { return k; }

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

        // 3) Drum-set switching via encoder button (BUTTON_LAST)
        bool thisEncoderButton = pod.encoder.Pressed();
        if (thisEncoderButton && !lastEncoderButton)
        {
            // On rising edge of encoder push: advance to next set
            currentDrumSet = (currentDrumSet + 1) % NUM_DRUM_SETS;
            // Apply new parameters immediately:
            kickOsc.SetFreq(     drumParams[currentDrumSet][0] );
            kickEnv.SetDecayTime( drumParams[currentDrumSet][1] );
            snareFilter.SetFreq( drumParams[currentDrumSet][2] );
            snareEnv.SetDecayTime( drumParams[currentDrumSet][3] );
        }
        lastEncoderButton = thisEncoderButton;

        // 4) Kick & Snare button edge detection
        bool thisButtonKick  = pod.button1.Pressed(); // Kick button
        bool thisButtonSnare = pod.button2.Pressed(); // Snare button

        if (thisButtonKick && !lastButtonKick)
            kickEnv.Retrigger(false);
        lastButtonKick = thisButtonKick;

        if (thisButtonSnare && !lastButtonSnare)
            snareEnv.Retrigger(false);
        lastButtonSnare = thisButtonSnare;

        // 5) Generate kick sample
        float k = kickOsc.Process() * kickEnv.Process(false) * 2.0f;

        // 6) Generate snare sample
        float noise    = snareNoise.Process();
        snareFilter.Process(noise);
        float filtered = snareFilter.Band();  // Use bandpass output
        float s        = filtered * snareEnv.Process(false);

        // 7) Mix kick + snare + click, apply volume
        float outSample = (k + s + clkSample) * volume;
        out[0][i] = outSample;
        out[1][i] = outSample;
    }
}

int main(void)
{
    pod.Init();
    sampleRate = pod.AudioSampleRate();


    // --- Initialize encoder state, but no additional setup needed for rotation here ---
    lastEncoderButton = pod.encoder.Pressed();

    // --- Kick drum initial setup (use parameters from set 0) ---
    kickOsc.Init(sampleRate);
    kickOsc.SetWaveform(Oscillator::WAVE_SIN);
    kickOsc.SetFreq(     drumParams[0][0] );
    kickEnv.Init(sampleRate);
    kickEnv.SetAttackTime( 0.001f );   // 1 ms attack
    kickEnv.SetDecayTime( drumParams[0][1] );
    kickEnv.SetSustainLevel(0.0f);

    // --- Snare drum initial setup (use parameters from set 0) ---
    snareNoise.Init();
    snareEnv.Init(sampleRate);
    snareEnv.SetAttackTime( 0.001f );   // 1 ms attack
    snareEnv.SetDecayTime( drumParams[0][3] );
    snareEnv.SetSustainLevel(0.0f);
    snareFilter.Init(sampleRate);
    snareFilter.SetFreq( drumParams[0][2] );
    snareFilter.SetRes(  0.7f );

    // --- Metronome click setup ---
    clickOsc.Init(sampleRate);
    clickOsc.SetWaveform(Oscillator::WAVE_SIN);
    clickOsc.SetFreq( 1000.0f );    // 1 kHz click
    clickEnv.Init(sampleRate);
    clickEnv.SetAttackTime(  0.0005f ); // 0.5 ms attack
    clickEnv.SetDecayTime(  0.01f );   // 10 ms decay
    clickEnv.SetSustainLevel(0.0f);

    pod.StartAdc();       // enable knob/button scanning
    pod.StartAudio(AudioCallback);

    while (1) { }
}
