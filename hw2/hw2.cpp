#include "daisy_pod.h"
#include "daisysp.h"
#include <math.h>

using namespace daisy;
using namespace daisysp;

// Audio processing constants
static const int DELAY_BUFFER_SIZE = 48000;  // 1 second at 48kHz sample rate
static const float PI = 3.14159265359f;
static const float TWO_PI = 2.0f * PI;

// Audio processing class for modulated delay effect
class ModulatedDelay {
private:
    // Delay line buffer and parameters
    float delay_buffer_[DELAY_BUFFER_SIZE];
    int write_index_;
    float delay_time_samples_;
    float feedback_amount_;
    float wet_dry_mix_;
    
    // Low-frequency oscillator (LFO) for modulation
    Oscillator lfo_;
    float lfo_depth_;
    float sample_rate_;
    
    // Input filtering for stability
    float input_filter_state_;
    float feedback_filter_state_;
    
public:
    void Init(float sample_rate) {
        sample_rate_ = sample_rate;
        
        // Initialize all variables to safe defaults
        write_index_ = 0;
        delay_time_samples_ = 0.1f * sample_rate;  // 100ms default delay
        feedback_amount_ = 0.3f;  // 30% feedback
        wet_dry_mix_ = 0.5f;      // 50/50 wet/dry mix
        lfo_depth_ = 0.2f;        // 20% modulation depth
        input_filter_state_ = 0.0f;
        feedback_filter_state_ = 0.0f;
        
        // Initialize LFO
        lfo_.Init(sample_rate);
        lfo_.SetWaveform(Oscillator::WAVE_SIN);
        lfo_.SetFreq(0.5f);  // 0.5 Hz default
        lfo_.SetAmp(1.0f);
        
        // Clear delay buffer
        for(int i = 0; i < DELAY_BUFFER_SIZE; i++) {
            delay_buffer_[i] = 0.0f;
        }
    }
    
    // Set delay parameters with safety bounds checking
    void SetDelayTime(float delay_seconds) {
        delay_seconds = daisysp::fmax(0.01f, daisysp::fmin(0.9f, delay_seconds));  // 10ms to 900ms
        delay_time_samples_ = delay_seconds * sample_rate_;
    }
    
    void SetFeedback(float feedback) {
        feedback_amount_ = daisysp::fmax(0.0f, daisysp::fmin(0.85f, feedback));  // Limit to prevent runaway feedback
    }
    
    void SetWetDryMix(float mix) {
        wet_dry_mix_ = daisysp::fmax(0.0f, daisysp::fmin(1.0f, mix));
    }
    
    // Set LFO modulation parameters
    void SetLFOFrequency(float frequency) {
        frequency = daisysp::fmax(0.01f, daisysp::fmin(10.0f, frequency));  // 0.01Hz to 10Hz
        lfo_.SetFreq(frequency);
    }
    
    void SetLFODepth(float depth) {
        lfo_depth_ = daisysp::fmax(0.0f, daisysp::fmin(0.8f, depth));  // 0% to 80% modulation
    }
    
    // Process audio sample with modulated delay
    float Process(float input) {
        // Apply gentle high-pass filter to input to remove DC offset
        input_filter_state_ += 0.001f * (input - input_filter_state_);
        float filtered_input = input - input_filter_state_;
        
        // Generate LFO modulation
        float lfo_value = lfo_.Process();
        
        // Calculate modulated delay time
        float modulated_delay = delay_time_samples_ * (1.0f + lfo_depth_ * lfo_value);
        modulated_delay = daisysp::fmax(1.0f, daisysp::fmin(DELAY_BUFFER_SIZE - 1.0f, modulated_delay));
        
        // Calculate read position with linear interpolation
        float read_pos = write_index_ - modulated_delay;
        if(read_pos < 0) {
            read_pos += DELAY_BUFFER_SIZE;
        }
        
        // Linear interpolation between adjacent samples
        int read_index1 = (int)read_pos;
        int read_index2 = (read_index1 + 1) % DELAY_BUFFER_SIZE;
        float frac = read_pos - read_index1;
        
        float delayed_sample = delay_buffer_[read_index1] * (1.0f - frac) + 
                              delay_buffer_[read_index2] * frac;
        
        // Apply feedback filtering to prevent high-frequency buildup
        feedback_filter_state_ += 0.3f * (delayed_sample * feedback_amount_ - feedback_filter_state_);
        
        // Write new sample to delay buffer (input + filtered feedback)
        delay_buffer_[write_index_] = filtered_input + feedback_filter_state_;
        
        // Advance write index
        write_index_ = (write_index_ + 1) % DELAY_BUFFER_SIZE;
        
        // Mix wet and dry signals
        return filtered_input * (1.0f - wet_dry_mix_) + delayed_sample * wet_dry_mix_;
    }
};

// Global objects
DaisyPod hw;
ModulatedDelay delay_processor;

// Control variables
float delay_time_s = 0.1f;
float feedback_amount = 0.3f;
float wet_dry_mix = 0.5f;
float lfo_rate_hz = 0.5f;
float lfo_depth = 0.2f;

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
    // Process audio samples
    for(size_t i = 0; i < size; i++) {
        // Get mono input (average L+R if stereo input)
        float input_sample = (in[0][i] + in[1][i]) * 0.5f;
        
        // Process through modulated delay
        float output_sample = delay_processor.Process(input_sample);
        
        // Apply soft limiting to prevent clipping
        output_sample = tanhf(output_sample * 0.8f);
        
        // Output to both channels
        out[0][i] = output_sample;
        out[1][i] = output_sample;
    }
}

int main(void) {
    // Initialize the Daisy Pod
    hw.Init();
    hw.SetAudioBlockSize(48); // 48 samples = 1ms at 48kHz
    float sample_rate = hw.AudioSampleRate();
    
    // Initialize delay processor
    delay_processor.Init(sample_rate);
    
    // Initialize LED to indicate system status
    hw.led1.Set(0.0f, 1.0f, 0.0f);  // Green LED = ready
    hw.UpdateLeds();
    
    // Start audio processing
    hw.StartAdc();
    hw.StartAudio(AudioCallback);
    
    // Variables for LED blinking and control processing
    uint32_t last_led_blink_time = System::GetNow();
    uint32_t last_control_time = System::GetNow();
    bool led_state = false;
    
    // Main loop - handle controls and LED
    while(1) {
        uint32_t now = System::GetNow();
        
        // Process controls every 10ms
        if(now - last_control_time > 10) {
            hw.ProcessAllControls();
            
            // Map knobs to parameters
            // Knob 1: Delay Time (10ms to 500ms)
            delay_time_s = 0.01f + hw.knob1.Value() * 0.49f;
            delay_processor.SetDelayTime(delay_time_s);
            
            // Knob 2: Feedback Amount (0% to 75%)
            feedback_amount = hw.knob2.Value() * 0.75f;
            delay_processor.SetFeedback(feedback_amount);
            
            // Encoder: Wet/Dry Mix (0% to 100% wet)
            hw.encoder.Debounce();
            int32_t encoder_increment = hw.encoder.Increment();
            if(encoder_increment != 0) {
                wet_dry_mix += encoder_increment * 0.05f;
                wet_dry_mix = daisysp::fmax(0.0f, daisysp::fmin(1.0f, wet_dry_mix));
                delay_processor.SetWetDryMix(wet_dry_mix);
            }
            
            // Button 1: LFO Rate (slow/fast when pressed)
            lfo_rate_hz = hw.button1.Pressed() ? 3.0f : 0.5f;
            delay_processor.SetLFOFrequency(lfo_rate_hz);
            
            // Button 2: LFO Depth (50% when pressed, 10% when released)
            lfo_depth = hw.button2.Pressed() ? 0.5f : 0.1f;
            delay_processor.SetLFODepth(lfo_depth);
            
            last_control_time = now;
        }
        
        // Blink LED every 500ms to show system is running
        if(now - last_led_blink_time > 500) {
            last_led_blink_time = now;
            led_state = !led_state;
            
            if(led_state) {
                hw.led1.Set(0.0f, 1.0f, 0.0f); // Green
            } else {
                hw.led1.Set(0.0f, 0.3f, 0.3f); // Dim cyan
            }
            hw.UpdateLeds();
        }
        
        System::Delay(1); // Small delay
    }
}