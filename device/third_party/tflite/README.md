# third_party/tflite

`tensorflow/lite/delegates/xnnpack/xnnpack_delegate.h`, copied unchanged from TensorFlow
**v2.21.0** (commit a481b10260dfdf833a1b16007eead49c1d7febf3), Apache License 2.0
(header kept in the file).

Why: the oss.qnx.com `tflite-runtime` 2.21.0 package exports the XNNPACK delegate functions from
`libtensorflow-lite.so` (checked on the Pi with `nm -D`), but `tflite-runtime-dev` doesn't ship this
header. Without it the uint8 detector ran on TFLite's slow reference kernels (~360 ms per frame on
the Pi 5). Only `detect/tflite_detector.cpp` includes it.
