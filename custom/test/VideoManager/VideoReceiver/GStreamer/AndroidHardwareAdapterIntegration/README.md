# Android hardware adapter host integration test

This is an opt-in standalone qmake test. The normal QGC/APK build does not
compile it, register its fake plugin or depend on its host libraries.

It compiles the production H.264 adapter, H.265 adapter and decoder policy,
then uses real GStreamer `h264parse`, `h265parse`, `decodebin3` and output pads.
A test-only `androidmedia` plugin exposes strict byte-stream/AU input caps;
its inner decoder is explicitly created `avdec_h264`/`avdec_h265`. These are
host software stand-ins, **not Android MediaCodec or hardware evidence**.

The five checks cover:

- The policy registers the AVC adapter at rank 356 and disables software autoplug.
- Excluding that adapter reproduces AVC negotiation failure with zero output.
- Restoring it converts AVC into Annex-B with SPS/PPS/IDR and produces decoded
  frames, concurrently with the existing HVC1 adapter path. Leaky latest-frame
  queues may drop frames; both outputs must contain at least one frame.
- Separate adapter instances own separate decoder objects.
- A failed internal decoder retains the adapter's ALWAYS pads and rejects READY.

The failed-instance check deliberately supplies an invalid inner test decoder;
its missing-pad warning and the production adapter's refusal log are expected.
The test does not verify RTSP transport, Android codec resource limits, GLMemory,
QML sinks or screen output. Those still require a device run with first-decoder
and first-sink-frame evidence.

## Requirements and run

Use a C++17 compiler, Qt Core (Qt 6 preferred) and GStreamer runtime/development
SDK of matching architecture. Required plugins: coreelements, videotestsrc,
x264, videoparsersbad, playback and libav. Validated with MSVC x64, Qt 5.14.2
and the official GStreamer 1.22.12 MSVC x64 packages.

From a compiler environment, use an out-of-tree build directory. For Windows,
set `GST_TEST_SDK` to the prefix containing `bin`, `include` and `lib`; add its
`bin` and Qt's `bin` to PATH. Set `GST_PLUGIN_PATH` to
`%GST_TEST_SDK%\lib\gstreamer-1.0` and `GST_REGISTRY` to a file in the test build
directory to keep the test registry separate.

```text
qmake REPO/custom/test/VideoManager/VideoReceiver/GStreamer/AndroidHardwareAdapterIntegration/AndroidHardwareAdapterIntegrationTest.pro
nmake
release\AndroidHardwareAdapterIntegrationTest.exe
```

On Linux use `make` and run `./AndroidHardwareAdapterIntegrationTest`;
GStreamer headers/libraries are resolved using pkg-config.

Qt 5 requires one compatibility preparation before qmake:

```text
python REPO/custom/test/VideoManager/VideoReceiver/GStreamer/AndroidHardwareAdapterIntegration/prepare_qt5_compat.py --build-dir BUILD_DIR
```

This creates only an out-of-tree H.265 source copy with
`s_registeredElementFactoryNames = {` replaced by
`s_registeredElementFactoryNames = QStringList{`, resolving Qt 5's ambiguous
assignment. No caps, decoder, initialization, teardown or state logic changes.
H.264 and policy sources are always included directly. The wrapper includes
host Qt headers before enabling the production Android/GStreamer conditional
branches; its logging shim uses Qt logging without QGC's application registry.

## Synthetic HEVC input

`SyntheticHevc.h` embeds 12 newly generated black 64x48 HEVC frames (1196 bytes)
as base64. It contains no third-party imagery. It removes encoder metadata SEI
and needs no x265 GStreamer plugin at test runtime. Generated with FFmpeg 7.1.1:

```text
ffmpeg -hide_banner -loglevel error -f lavfi -i color=black:size=64x48:rate=25 -frames:v 12 -c:v libx265 -preset ultrafast -x265-params log-level=error:keyint=1 -bsf:v filter_units=remove_types=39 -f hevc synthetic.h265
```

Base64 of that file is `syntheticHevcBase64`; the test writes it to an automatically
removed temporary file. H.264 input is generated directly using `videotestsrc`
and `x264enc`, then parsed to the same AVC/AU contract observed in the Rouke log.
