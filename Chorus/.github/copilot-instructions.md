# Chorus Audio Plugin - AI Coding Assistant Instructions

## Project Overview
This is a JUCE-based audio effect plugin implementing a chorus effect, based on the book "Audio Effects: Theory, Implementation and Application" by Joshua D. Reiss and Andrew P. McPherson. The plugin creates a chorus effect through multi-voice delay modulation using an LFO.

## Architecture
- **PluginProcessor**: Core audio processing logic, parameter management, and chorus algorithm implementation
- **PluginEditor**: UI components and layout using JUCE's component system
- **PluginParameter**: Custom parameter wrapper classes around AudioProcessorValueTreeState for type-safe parameter handling
- **CustomLookAndFeel**: UI styling overrides (Arial 24pt font for labels)

## Key Components

### Audio Processing (PluginProcessor)
- Implements multi-voice chorus using delay buffers with LFO modulation
- Supports 2-5 voices with configurable stereo spreading
- LFO waveforms: Sine, Triangle, Sawtooth (rising/falling)
- Interpolation modes: None (nearest neighbor), Linear, Cubic
- Parameters smoothed using LinearSmoothedValue for glitch-free modulation

### Parameter System
Custom parameter classes in `PluginParameter.h`:
- `PluginParameterLinSlider`: Linear sliders with optional value transformation callbacks
- `PluginParameterLogSlider`: Logarithmic sliders for frequency parameters
- `PluginParameterToggle`: Boolean parameters
- `PluginParameterComboBox`: Enumeration parameters with string arrays

Parameter naming convention: IDs are lowercase with spaces removed (e.g., "LFO Frequency" → "lfofrequency")

### UI Layout (PluginEditor)
- Fixed 500px width layout with 10px margins
- Components arranged vertically: sliders, toggles, combo boxes
- Attachments automatically connect UI to AudioProcessorValueTreeState

## Build System
```bash
mkdir build && cd build
cmake ..
make
```

Built binaries appear in `Chorus_artefacts/` directory.

## Development Workflow
1. Modify parameters in `PluginProcessor` constructor using `PluginParameter*` classes
2. Update UI in `PluginEditor::resized()` if adding/removing controls
3. Audio algorithm changes in `PluginProcessor::processBlock()`
4. Rebuild with `cd build && make` after code changes

## Code Patterns

### Parameter Creation
```cpp
// In PluginProcessor constructor
paramDelay (parameters, "Delay", "ms", 10.0f, 50.0f, 30.0f, [](float value){ return value * 0.001f; })
```
- Name, unit, min, max, default, optional transform callback

### Audio Buffer Processing
```cpp
for (int channel = 0; channel < numInputChannels; ++channel) {
    float* channelData = buffer.getWritePointer(channel);
    // Process samples...
}
```
- Always use `getWritePointer()` for channel data
- Handle both input and output channel counts

### Delay Buffer Management
- Circular buffer with `delayWritePosition` tracking
- Size calculated as `maxDelayTime * sampleRate + 1`
- Read positions use modulo arithmetic for wrapping

### LFO Implementation
- Phase normalized 0.0-1.0, converted to waveforms in `lfo()` method
- Phase increment: `phase += frequency * inverseSampleRate`

## Testing
- Standalone app builds for testing without DAW
- Max/MSP patch in `Products/Chorus.maxpat` for audio testing
- Parameters update in real-time during playback

## Dependencies
- JUCE 7.x (audio framework)
- CMake 3.15+ (build system)
- C++17 standard

## Common Tasks
- **Add parameter**: Create `PluginParameter*` member, initialize in constructor, add to UI in editor
- **Modify algorithm**: Edit `processBlock()` method, test with standalone build
- **Change UI**: Update `resized()` method and component arrays in editor
- **Debug audio**: Use standalone build, check parameter smoothing and buffer sizes</content>
<parameter name="filePath">/home/guthman/source/Audio-Effects/Chorus/.github/copilot-instructions.md