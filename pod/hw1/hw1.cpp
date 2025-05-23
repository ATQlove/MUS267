#include "daisy_pod.h"

using namespace daisy;

DaisyPod hw;

static void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    // Get the current knob position for volume control (0.0 to 1.0)
    float volume = hw.knob1.Process();
    
    // Process each sample
    for(size_t i = 0; i < size; i++)
    {
        // Pass input to output with volume control
        // For both left (0) and right (1) channels
        out[0][i] = in[0][i] * volume;
        out[1][i] = in[1][i] * volume;
    }
}

int main(void)
{
    bool brightness1, brightness2;
    brightness1 = false;
    brightness2 = false;
    
    hw.Init();
    
    hw.StartAdc();
    hw.SetAudioBlockSize(4); // Lower values for lower latency
    hw.StartAudio(AudioCallback);

    while(1)
    {
        // The function of this button is unrelated to the first assignment. 
        // I only used it to verify whether the code was successfully downloaded onto the board.
        
        hw.ProcessAnalogControls(); // Process knobs
        hw.ProcessDigitalControls(); // Process buttons

        // using button1 as momentary switch for turning on/off led1
        brightness1 = hw.button1.Pressed();

        // using button2 as latching switch for toggling led2
        if(hw.button2.RisingEdge())
            brightness2 = !brightness2;

        // assign brightness levels to each led (R, G, B)
        hw.led1.Set(brightness1, brightness1, brightness1);
        hw.led2.Set(brightness2, brightness2, brightness2);
        hw.UpdateLeds();
    }
}