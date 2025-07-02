# AEC plan

## Information

Goal: add AEC to esp32-s3-box-3 application

This repository from Espressif includes a number of audio processing capabilities for various ESP32 boards including the S3.

https://github.com/kwindla/esp-adf/

There is AEC support. An example is this file in the esp-adf repository: components/audio_stream/aec_stream.c

Note that RECORD_HARDWARE_AEC is defined as true in this file:

https://github.com/kwindla/esp-adf/blob/1e839afb5a5166aad6028ac2796272f32454dced/components/audio_board/esp32_s3_box_3/board_def.h#L82

This means that the S3 board has hardware support, so we should be able to use the hardware AEC on the board.

## Initial task

Develop a plan for adding hardware AEC to the esp32-s3-box-3 application, using the esp-adf repository as a reference.

### Clarifying questions and answers

  1. Current AEC Implementation: I noticed the code currently has a simple software-based echo cancellation (muting the mic when
  playing audio). Do you want to replace this entirely with hardware AEC, or have both options available?

  Answer: make both options available. Default to hardware AEC, but allow the setting to be changed at runtime. For example, the Pipecat process this client is connected to could send a message to enable or disable advanced AEC.

  2. ESP-ADF Integration: The ESP-ADF (Audio Development Framework) is a separate framework from ESP-IDF. Are you open to:
    - Adding ESP-ADF as a dependency to the project?
    - Or would you prefer extracting just the AEC components?
    - Or finding an ESP-IDF-native solution?

  Answer: We should add ESP-ADF as a dependency to the project.

  3. Audio Pipeline Changes: Hardware AEC typically requires changes to the audio pipeline structure. Are you comfortable with
  potentially significant changes to the current media.cpp implementation?

  Answer: Yes we should make changes to media.cpp as needed.

  4. Performance Requirements: What are your expectations for the AEC performance? The current approach (muting during playback)
  provides complete echo cancellation but prevents full-duplex communication. Hardware AEC would allow simultaneous speaking and
  listening.

  Answer: We need to support full-duplex communication. The user should be able to speak and listen at the same time.

  5. Compatibility: Should the plan maintain compatibility with the Linux build (which wouldn't have hardware AEC), or is this
  ESP32-specific?

  Answer: The advanced AEC can be ESP32-specific. The Linux build should continue to use the simple software-based echo cancellation.

## Implementation Plan

### Phase 1: Add ESP-ADF as a Dependency

1. **Add ESP-ADF as a submodule**
   ```bash
   cd esp32-s3-box-3/components
   git submodule add https://github.com/espressif/esp-adf.git
   ```

2. **Update CMakeLists.txt**
   - Add ESP-ADF components to the build system
   - Include required ADF components: `audio_stream`, `audio_pipeline`, `audio_hal`, `audio_board`
   - Ensure conditional compilation for ESP32 target only

3. **Configure ESP-ADF for ESP32-S3-BOX-3**
   - The board already has `RECORD_HARDWARE_AEC` defined as true
   - Verify audio codec compatibility with existing BSP

### Phase 2: Create AEC Configuration Interface

1. **Add AEC configuration structure** (in new file `src/aec_config.h`)
   ```cpp
   typedef struct {
       bool enabled;
       bool use_hardware_aec;  // true for hardware, false for software muting
       int filter_length;
       int mode;
   } aec_config_t;
   ```

2. **Add runtime configuration support**
   - Global AEC configuration variable
   - Function to update AEC settings from RTVI messages
   - Default to hardware AEC enabled

### Phase 3: Refactor Audio Pipeline

1. **Modify `media.cpp` structure**
   - Add conditional compilation for ESP32 target
   - Keep existing implementation for Linux build
   - For ESP32, create two audio pipeline paths:
     - Software AEC path (existing muting approach)
     - Hardware AEC path (using ESP-ADF)

2. **Implement ESP-ADF audio pipeline** (when hardware AEC enabled)
   ```
   Microphone → I2S Stream → AEC Stream → Opus Encoder → WebRTC
   Speaker ← I2S Stream ← Opus Decoder ← WebRTC
                ↓
           Reference signal to AEC
   ```

3. **Key implementation points**
   - Initialize ESP-ADF audio pipeline in `pipecat_init_audio_capture()`
   - Create AEC stream element using `aec_stream_init()`
   - Link pipeline elements: i2s_stream → aec_stream → encoder
   - Provide speaker output as reference signal to AEC
   - Handle audio format conversion (ESP-ADF uses 48kHz, our system uses 16kHz)

### Phase 4: Runtime Switching Implementation

1. **Add switching mechanism**
   - Function to stop current audio pipeline
   - Function to start new pipeline (software or hardware AEC)
   - Ensure smooth transition without dropping connection

2. **RTVI message handling**
   - Add new message type in `rtvi_callbacks.cpp` for AEC configuration
   - Message format: `{"type": "aec_config", "enabled": true, "use_hardware": true}`
   - Update configuration and restart audio pipeline if needed

### Phase 5: Audio Format Compatibility

1. **Sample rate conversion**
   - ESP-ADF board config uses 48kHz, but our WebRTC uses 16kHz
   - Add resampler element in pipeline or configure I2S for 16kHz
   - Ensure Opus encoder still receives correct format

2. **Buffer size adjustments**
   - Current implementation uses 20ms frames (320 samples at 16kHz)
   - Adjust buffer sizes for ESP-ADF pipeline compatibility
   - Maintain timing requirements for WebRTC

### Phase 6: Testing and Optimization

1. **Test scenarios**
   - Hardware AEC with full-duplex communication
   - Software AEC fallback
   - Runtime switching between modes
   - Performance impact measurement

2. **Optimization considerations**
   - CPU usage comparison between software and hardware AEC
   - Memory usage of ESP-ADF components
   - Latency measurements

### Technical Considerations

1. **Memory Management**
   - ESP-ADF requires additional memory for audio pipeline
   - May need to adjust partition table or memory allocation

2. **Thread Safety**
   - Audio pipeline runs in separate task
   - Ensure thread-safe access to configuration
   - Protect pipeline state during runtime switching

3. **Error Handling**
   - Graceful fallback to software AEC if hardware AEC fails
   - Clear error reporting through logs

4. **Backwards Compatibility**
   - Maintain existing behavior by default
   - Only use hardware AEC when explicitly enabled
   - Linux build remains unchanged

### Dependencies and Risks

1. **Dependencies**
   - ESP-ADF framework (new submodule)
   - Additional ESP-IDF components required by ADF
   - Increased binary size

2. **Risks**
   - ESP-ADF version compatibility with current ESP-IDF version
   - Potential conflicts between BSP and ADF audio handling
   - Increased complexity in build system

### Estimated Implementation Time

- Phase 1: 2-3 hours (dependency setup)
- Phase 2: 1-2 hours (configuration interface)
- Phase 3: 4-6 hours (pipeline refactoring)
- Phase 4: 2-3 hours (runtime switching)
- Phase 5: 2-3 hours (format compatibility)
- Phase 6: 2-3 hours (testing)

Total: 13-20 hours of development time