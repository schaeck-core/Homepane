# TensorFlow Lite Micro "microfrontend" (Apache 2.0, see LICENSE): the audio
# feature extraction (window -> FFT -> mel filterbank -> noise reduction ->
# PCAN -> log) that microWakeWord models are trained on. espressif/esp-tflite-micro
# references but does not ship these sources; copied unchanged from
# github.com/tensorflow/tflite-micro (commit ff8cff73024a),
# tensorflow/lite/experimental/microfrontend/lib.
set(lib "tensorflow/lite/experimental/microfrontend/lib")
idf_component_register(
    SRCS "${lib}/fft.cc" "${lib}/fft_util.cc" "${lib}/filterbank.c" "${lib}/filterbank_util.c"
         "${lib}/frontend.c" "${lib}/frontend_util.c" "${lib}/kiss_fft_int16.cc" "${lib}/log_lut.c"
         "${lib}/log_scale.c" "${lib}/log_scale_util.c" "${lib}/noise_reduction.c"
         "${lib}/noise_reduction_util.c" "${lib}/pcan_gain_control.c" "${lib}/pcan_gain_control_util.c"
         "${lib}/window.c" "${lib}/window_util.c"
    INCLUDE_DIRS "."
    REQUIRES espressif__esp-tflite-micro)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-variable -Wno-unused-function)
