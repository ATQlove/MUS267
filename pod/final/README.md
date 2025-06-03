# Pod Modulated Delay Processor (DSP)

## Author

Kai Wang

## Description:

This assignment2 project implements a high-quality modulated delay effect for the Daisy Pod. The delay line is modulated by a low-frequency oscillator (LFO) to create chorus, flanger, and vibrato-like effects. The processor includes feedback control, wet/dry mixing, and real-time parameter adjustment via the Pod's knobs and buttons.

## Audio Processing Features

### Core Components
1. **Delay Line**: Up to 1 second of delay with linear interpolation for smooth modulation
2. **LFO Modulation**: Sine wave oscillator that modulates the delay time
3. **Feedback Loop**: Controlled regeneration with high-frequency filtering for stability
4. **Wet/Dry Mixing**: Blend between processed and original signal
5. **Safety Limiting**: Soft saturation prevents clipping and distortion

### Technical Specifications
- **Sample Rate**: 48 kHz
- **Delay Range**: 10ms to 500ms
- **LFO Range**: 0.2 Hz to 16 Hz
- **Modulation Depth**: 5% to 80%
- **Feedback Range**: 0% to 75% (limited for stability)
- **Audio Latency**: ~1ms (48-sample blocks)

## Control Interface

### Knob 1 - Delay Time
- **Function**: Sets the base delay time before modulation
- **Range**: 10ms to 500ms
- **Position**: Fully counter-clockwise = shortest delay (10ms), fully clockwise = longest delay (500ms)
- **Effect**: Shorter delays create flanger effects, longer delays create echo effects

### Knob 2 - Feedback Amount  
- **Function**: Controls how much delayed signal is fed back into the delay line
- **Range**: 0% to 75%
- **Position**: Fully counter-clockwise = no feedback, fully clockwise = maximum feedback
- **Effect**: Higher feedback creates more repeats and resonance
- **Safety**: Limited to 75% to prevent runaway feedback and instability

### Encoder - Wet/Dry Mix
- **Function**: Blends between dry (original) and wet (processed) signal
- **Range**: 0% to 100% wet
- **Operation**: Turn encoder clockwise to increase wet signal, counter-clockwise to increase dry signal
- **Effect**: 0% = dry only, 50% = equal mix, 100% = wet only

### Button 1 - LFO Rate Control
- **Function**: Changes LFO frequency for modulation speed
- **Operation**: 
  - Released: LFO runs at slow rate (0.2 Hz - one cycle every 5 seconds)
  - Pressed: LFO runs at fast rate (16 Hz - 16 cycles per second)
- **Effect**: 
  - Slow: Creates smooth, gentle "floating" effect
  - Fast: Creates rapid "tremolo/vibrato" effect with obvious pitch modulation

### Button 2 - LFO Depth Control
- **Function**: Controls how much the LFO modulates the delay time
- **Operation**:
  - Released: Light modulation (5% depth - subtle effect)
  - Pressed: Heavy modulation (80% depth - dramatic effect)
- **Effect**: 
  - Light: Minimal delay time variation, subtle effect
  - Heavy: Large delay time changes, pronounced pitch bending and "swaying" sensation

## LED Status Indicators

### Normal Operation (No buttons pressed)
- **Both LEDs**: Green breathing pattern (bright → dim → bright)
- **Indicates**: System running normally, ready for input

### Button 1 Pressed
- **LED1**: Solid blue
- **LED2**: Keep the status before you press button1
- **Indicates**: Fast LFO mode active (16 Hz modulation)

### Button 2 Pressed
- **LED1**: Keep the status before you press button2 
- **LED2**: Solid blue
- **Indicates**: Deep modulation mode active (80% depth)

### Both Buttons Pressed
- **LED1**: Solid blue (fast LFO)
- **LED2**: Solid blue (deep modulation)
- **Indicates**: Maximum modulation effect active

## Usage Examples

### Chorus Effect
1. Set Knob 1 (Delay Time) to 20% (~110ms delay)
2. Set Knob 2 (Feedback) to 15% (light feedback)
3. Set Encoder (Mix) to 50% (equal wet/dry)
4. Keep both buttons released (slow LFO, light modulation)
5. **Result**: Classic chorus sound with gentle modulation

### Flanger Effect  
1. Set Knob 1 (Delay Time) to 5% (~35ms very short delay)
2. Set Knob 2 (Feedback) to 50% (moderate feedback)
3. Set Encoder (Mix) to 70% (mostly wet)
4. Press Button 1 (fast LFO)
5. Press Button 2 (heavy modulation)
6. **Result**: Dramatic flanger "swoosh" effect

### Vibrato Effect
1. Set Knob 1 (Delay Time) to 15% (~85ms short delay)
2. Set Knob 2 (Feedback) to 0% (no feedback)
3. Set Encoder (Mix) to 100% (full wet signal)
4. Press Button 1 (fast LFO)
5. Press Button 2 (heavy modulation)
6. **Result**: Pure pitch vibrato effect

### Ambient Delay with Movement
1. Set Knob 1 (Delay Time) to 80% (~410ms long delay)
2. Set Knob 2 (Feedback) to 40% (moderate feedback for repeats)
3. Set Encoder (Mix) to 30% (mostly dry with ambient echoes)
4. Keep buttons released (slow, subtle modulation)
5. **Result**: Spacious delay with gentle movement

### Extreme Modulation Effect
1. Set Knob 1 (Delay Time) to 50% (~260ms medium delay)
2. Set Knob 2 (Feedback) to 60% (high feedback)
3. Set Encoder (Mix) to 80% (mostly wet)
4. Press both buttons (fast LFO + heavy modulation)
5. **Result**: Dramatic, otherworldly modulation effect







