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

//Hi-hat// ———— 改为 Hi-Hat：
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


// ==================== 全局新增（放在 currentDrumSet 定义之后） ====================
static bool  presetMode        = false;
static bool  presetPlaying     = false;
static int   presetStep        = 0;
static float subdivCounter     = 0.0f;
static float subdivIntervalSamples = 0.0f;

// ———— 将 PRESET_STEPS 从 8 改为 32 ————
constexpr int PRESET_STEPS = 32;

// ———— 四小节共 32 步的预设节奏 ——//
// B 行：Bass drum （Kick）
static const uint8_t presetBass[PRESET_STEPS] = {
    // bar1            bar2            bar3            bar4
    1,1,0,0,0,0,1,0,  1,1,0,0,0,0,1,0,  1,1,0,0,0,1,0,0,  0,1,0,0,0,1,0,0
};

// S 行：Snare
static const uint8_t presetSnare[PRESET_STEPS] = {
    // bar1            bar2            bar3            bar4
    0,0,1,1,0,1,1,0,  0,0,1,1,0,1,1,0,  0,0,1,1,0,0,0,1,  0,1,1,0,1,1,0,1
};

// R 行：Ride/Hi-Hat
static const uint8_t presetClick[PRESET_STEPS] = {
    // bar1            bar2            bar3            bar4
    1,1,1,1,1,1,1,1,  1,1,1,1,1,1,1,1,  1,1,1,1,1,1,1,1,  1,1,1,1,0,1,1,0
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

        // —— 改为：生成 Hi-Hat —— 
        float noiseSample = hiHatNoise.Process();
        hiHatFilter.Process(noiseSample);
        float hHatSample = hiHatFilter.High() * hiHatEnv.Process(false);

        // 3) Drum-set switching via encoder rotation
        int32_t encoderIncrement = pod.encoder.Increment();
        if (encoderIncrement != 0)
        {
            // On encoder rotation: advance to next/previous set
            currentDrumSet = (currentDrumSet + encoderIncrement + NUM_DRUM_SETS) % NUM_DRUM_SETS;
            // Apply new parameters immediately:
            kickOsc.SetFreq(     drumParams[currentDrumSet][0] );
            kickEnv.SetDecayTime( drumParams[currentDrumSet][1] );
            snareFilter.SetFreq( drumParams[currentDrumSet][2] );
            snareEnv.SetDecayTime( drumParams[currentDrumSet][3] );
            
            // Update LED color for the current drum set
            pod.led1.Set(drumSetColors[currentDrumSet][0] / 255.0f,
                        drumSetColors[currentDrumSet][1] / 255.0f,
                        drumSetColors[currentDrumSet][2] / 255.0f);
            pod.led1.Update();
        }

        // 重新计算 1/8 音符的样本间隔
        subdivIntervalSamples = beatIntervalSamples * 0.5f;

        // —— 新：1/8 拆分计时 —— 
        subdivCounter += 1.0f;
        bool newSubdivision = false;
        if (subdivCounter >= subdivIntervalSamples)
        {
            subdivCounter -= subdivIntervalSamples;
            newSubdivision = true;
        }

        // —— 新：检测 Encoder 按下边沿 —— 
        bool thisEncoderBtn = pod.encoder.Pressed();
        if (thisEncoderBtn && !lastEncoderButton)
        {
            presetMode = !presetMode;  // 切换模式
            // 可选：用不同颜色提示
            if (presetMode)
                pod.led1.Set(1.0f, 1.0f, 1.0f);  // 白灯表示预设
            else
                pod.led1.Set( drumSetColors[currentDrumSet][0]/255.0f,
                              drumSetColors[currentDrumSet][1]/255.0f,
                              drumSetColors[currentDrumSet][2]/255.0f );
            pod.led1.Update();
        }
        lastEncoderButton = thisEncoderBtn;

        // —— 新：处理预设模式下的按钮 —— 
        bool thisKickBtn  = pod.button1.Pressed();
        bool thisSnareBtn = pod.button2.Pressed();

        if (!presetMode)
        {
            // 手动模式，保持原有逻辑
            if (thisKickBtn  && !lastButtonKick)  kickEnv.Retrigger(false);
            if (thisSnareBtn && !lastButtonSnare) snareEnv.Retrigger(false);
        }
        else
        {
            // 预设模式：单击 Kick 启动节奏播放
            if (thisKickBtn && !lastButtonKick && !presetPlaying)
            {
                presetPlaying = true;
                presetStep    = 0;
                subdivCounter = 0.0f;
            }
        }
        lastButtonKick  = thisKickBtn;
        lastButtonSnare = thisSnareBtn;

        // —— 新：在预设播放中，每到 1/8 拍触发对应鼓声 —— 
        if (presetMode && presetPlaying && newSubdivision)
        {
            if (presetBass[presetStep])  kickEnv.Retrigger(false);
            if (presetSnare[presetStep]) snareEnv.Retrigger(false);
            // if (presetClick[presetStep]) clickEnv.Retrigger(false);
            if (presetClick[presetStep]) hiHatEnv.Retrigger(false);

            // 播完 32 步后退出
            presetStep++;
            if(presetStep >= PRESET_STEPS)
            {
                presetPlaying = false;
                // （可选）恢复当前套鼓的 LED
                pod.led1.Set( drumSetColors[currentDrumSet][0]/255.0f,
                            drumSetColors[currentDrumSet][1]/255.0f,
                            drumSetColors[currentDrumSet][2]/255.0f );
                pod.led1.Update();
            }

        }


        // 5) Generate kick sample
        float k = kickOsc.Process() * kickEnv.Process(false) * 2.5f;

        // 6) Generate snare sample
        float noise    = snareNoise.Process();
        snareFilter.Process(noise);
        float filtered = snareFilter.Band();  // Use bandpass output
        float s        = filtered * snareEnv.Process(false);

        // 7) Mix kick + snare + click, apply volume
        float outSample;
        if (!presetMode)
        {
            // 普通模式：使用 metronome
            outSample = (k + s + clkSample) * volume;
        }
        else
        {
            // 预设模式：同时使用 metronome 和 hi-hat
            outSample = (k + s + clkSample + hHatSample) * volume;
        }
        out[0][i] = outSample;
        out[1][i] = outSample;
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

    // --- Hi-Hat initial setup ————
    hiHatNoise.Init();
    hiHatEnv.Init(sampleRate);
    hiHatEnv.SetAttackTime(0.001f);   // 1 ms
    hiHatEnv.SetDecayTime(0.05f);     // 短衰减
    hiHatEnv.SetSustainLevel(0.0f);
    hiHatFilter.Init(sampleRate);
    // 用高通滤波器保留高频部分
    hiHatFilter.SetFreq(8000.0f);
    hiHatFilter.SetRes(0.7f);

    pod.StartAdc();       // enable knob/button scanning
    pod.StartAudio(AudioCallback);

    while (1) { }
}
