# External API Reference — `toslink_play.cpp`

Every non-standard-library function called in the program, grouped by header,
with signature, short description, and a link to the upstream documentation.
Doxygen pages reflect the FFmpeg `trunk` and current ALSA `alsa-lib` docs —
signatures match FFmpeg 4.4.2 / libavformat 58.x (Ubuntu 22.04), which is what
this program is built against.

---

## FFmpeg — `libavformat/avformat.h`

Demuxer/container-level API: opening the spdif "container", finding its one
stream, reading compressed packets out of it.

| Function | Description |
|---|---|
| `AVFormatContext *avformat_alloc_context(void)` | Allocates an empty `AVFormatContext`, which we then wire up with a custom `AVIOContext` before opening. |
| `AVInputFormat *av_find_input_format(const char *short_name)` | Looks up a demuxer by name — used to force `"spdif"` instead of letting FFmpeg auto-probe the format. |
| `int avformat_open_input(AVFormatContext **ps, const char *url, const AVInputFormat *fmt, AVDictionary **options)` | Opens the input and runs the demuxer's header/probe logic (for spdif, this is where sync-word detection happens). |
| `int avformat_find_stream_info(AVFormatContext *ic, AVDictionary **options)` | Reads ahead to fully populate stream parameters (codec, channels, rate) — required after `avformat_open_input` for raw/compressed formats like spdif. |
| `void avformat_close_input(AVFormatContext **s)` | Closes the input and frees the `AVFormatContext`. |
| `int av_read_frame(AVFormatContext *s, AVPacket *pkt)` | Reads the next demuxed packet (one compressed AC-3 frame) from the stream. |

Docs: <https://ffmpeg.org/doxygen/trunk/avformat_8h.html>

---

## FFmpeg — `libavformat/avio.h`

Custom I/O glue — lets libavformat pull bytes from our own ALSA read callback
instead of a file.

| Function | Description |
|---|---|
| `AVIOContext *avio_alloc_context(unsigned char *buffer, int buffer_size, int write_flag, void *opaque, int (*read_packet)(void*, uint8_t*, int), int (*write_packet)(void*, const uint8_t*, int), int64_t (*seek)(void*, int64_t, int))` | Wraps our `avio_read_cb` (which itself calls `snd_pcm_readi`) as a generic byte source libavformat can demux from. |
| `void avio_context_free(AVIOContext **s)` | Frees the `AVIOContext` (the backing buffer is freed separately via `av_freep`). |

Docs: <https://ffmpeg.org/doxygen/trunk/avio_8h.html>

---

## FFmpeg — `libavcodec/avcodec.h`

Decoder-level API: finding and running the AC-3 decoder, packet/frame
allocation.

| Function | Description |
|---|---|
| `const AVCodec *avcodec_find_decoder(enum AVCodecID id)` | Looks up the decoder matching the codec ID the demuxer reported (AC-3 in this program). |
| `AVCodecContext *avcodec_alloc_context3(const AVCodec *codec)` | Allocates a decoder context for the found codec. |
| `int avcodec_parameters_to_context(AVCodecContext *codec, const AVCodecParameters *par)` | Copies the stream's codec parameters (from the demuxer) into the decoder context before opening it. |
| `int avcodec_open2(AVCodecContext *avctx, const AVCodec *codec, AVDictionary **options)` | Initializes the decoder — must be called before sending any packets. |
| `void avcodec_free_context(AVCodecContext **avctx)` | Frees the decoder context. |
| `int avcodec_send_packet(AVCodecContext *avctx, const AVPacket *avpkt)` | Feeds one compressed AC-3 packet into the decoder. |
| `int avcodec_receive_frame(AVCodecContext *avctx, AVFrame *frame)` | Pulls one decoded PCM frame back out of the decoder. |
| `AVPacket *av_packet_alloc(void)` | Allocates a reusable `AVPacket` for demuxed compressed data. |
| `void av_packet_unref(AVPacket *pkt)` | Releases the packet's reference-counted buffer after use, without freeing the `AVPacket` struct itself. |
| `AVFrame *av_frame_alloc(void)` | Allocates a reusable `AVFrame` for decoded PCM output. |

Docs: <https://ffmpeg.org/doxygen/trunk/avcodec_8h.html>

---

## FFmpeg — `libavutil` (`mem.h`, `error.h`, `channel_layout.h`)

Small utility calls used alongside the above.

| Function | Header | Description |
|---|---|---|
| `void *av_malloc(size_t size)` | `mem.h` | Allocates the backing buffer handed to `avio_alloc_context`. |
| `void av_freep(void *ptr)` | `mem.h` | Frees a pointer and sets it to `NULL` — used for the `AVIOContext` buffer. |
| `int av_strerror(int errnum, char *errbuf, size_t errbuf_size)` | `error.h` | Converts an FFmpeg error code (`AVERROR(...)`) into a human-readable string for logging. |
| `int64_t av_get_default_channel_layout(int nb_channels)` | `channel_layout.h` | Fallback channel-layout guess when the decoded frame doesn't report one explicitly. |

Docs: <https://ffmpeg.org/doxygen/trunk/mem_8h.html> · <https://ffmpeg.org/doxygen/trunk/error_8h.html> · <https://ffmpeg.org/doxygen/trunk/channel__layout_8h.html>

---

## FFmpeg — `libswresample/swresample.h`

Sample-format/channel-layout conversion from the decoder's native output to
interleaved S16LE, 5.1.

| Function | Description |
|---|---|
| `SwrContext *swr_alloc_set_opts(SwrContext *s, int64_t out_ch_layout, enum AVSampleFormat out_sample_fmt, int out_sample_rate, int64_t in_ch_layout, enum AVSampleFormat in_sample_fmt, int in_sample_rate, int log_offset, void *log_ctx)` | Allocates and configures the resampler/format-converter context for one decode session. |
| `int swr_init(SwrContext *s)` | Initializes the configured `SwrContext`, must be called before `swr_convert`. |
| `int swr_convert(SwrContext *s, uint8_t **out, int out_count, const uint8_t **in, int in_count)` | Converts one decoded frame's samples into interleaved S16LE. |
| `void swr_free(SwrContext **s)` | Frees the resampler context. |

Docs: <https://ffmpeg.org/doxygen/trunk/swresample_8h.html>

---

## ALSA — `alsa/asoundlib.h`

All ALSA calls come from this single umbrella header; grouped below by the
ALSA library's own documentation sections (PCM core, hardware params,
software params) since that's how upstream organizes them.

### PCM core (open/close/transfer)

| Function | Description |
|---|---|
| [`int snd_pcm_open(snd_pcm_t **pcm, const char *name, snd_pcm_stream_t stream, int mode)`](https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m.html#ga8340c7dc0ac37f37afe5e7c21d6c528b) | Opens a capture or playback PCM device by ALSA name (e.g. `hw:CARD=...`, `Surround`). |
| [`int snd_pcm_close(snd_pcm_t *pcm)`](https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m.html#ga042aba7262a4cbb4d444b6fc08cb7124) | Closes a PCM handle. |
| [`int snd_pcm_prepare(snd_pcm_t *pcm)`](https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m.html#ga788d05de75f2d536f8443cb0306754d0) | Brings a configured PCM stream to the `PREPARED` state, ready to start. |
| [`int snd_pcm_start(snd_pcm_t *pcm)`](https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m.html#ga6bdb88b68a9d9e66015d770f600c6aea) | Explicitly starts a capture stream (needed here since capture doesn't auto-start the way playback does). |
| [`int snd_pcm_wait(snd_pcm_t *pcm, int timeout)`](https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m.html#gad4d53d58b996a7cd9a5cbf1710b90375) | Blocks (with a timeout) until the PCM device is ready for I/O — used both for capture-ready polling and to detect "no signal" via timeout. |
| [`snd_pcm_sframes_t snd_pcm_readi(snd_pcm_t *pcm, void *buffer, snd_pcm_uframes_t size)`](https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m.html#ga4c2c7bd26cf221268d59dc3bbeb9c048) | Reads interleaved frames from a capture device. |
| [`snd_pcm_sframes_t snd_pcm_writei(snd_pcm_t *pcm, const void *buffer, snd_pcm_uframes_t size)`](https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m.html#gabc748a500743713eafa960c7d104ca6f) | Writes interleaved frames to a playback device. |
| [`int snd_pcm_recover(snd_pcm_t *pcm, int err, int silent)`](https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m.html#ga2157aaeb6fc14da3f040d76591f9d3b1) | Standard recovery helper for xruns (`-EPIPE`) and suspends — calls `snd_pcm_prepare()` internally as needed. |
| [`const char *snd_strerror(int errnum)`](https://www.alsa-project.org/alsa-doc/alsa-lib/group___error.html) | Converts an ALSA/errno error code into a human-readable string for logging. *(Linked to the parent "Error handling" group — the current doxygen build doesn't expose a stable per-function anchor for this one.)* |

Docs: <https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m.html>

### Hardware params (`snd_pcm_hw_params_t`)

Configuring rate, format, channels, period/buffer size — negotiated once per
device at startup.

| Function | Description |
|---|---|
| [`snd_pcm_hw_params_alloca(snd_pcm_hw_params_t **ptr)`](https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m___h_w___params.html#ga06b83cb9a788f99b7b09b570b4355cee) *(macro)* | Stack-allocates a `hw_params` struct. |
| [`int snd_pcm_hw_params_any(snd_pcm_t *pcm, snd_pcm_hw_params_t *params)`](https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m___h_w___params.html#ga6e2dd8efbb7a4084bd05e6cc458d84f7) | Fills the struct with the device's full range of supported configurations. |
| [`int snd_pcm_hw_params_set_access(...)`](https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m___h_w___params.html) † | Selects interleaved read/write access. |
| [`int snd_pcm_hw_params_set_format(...)`](https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m___h_w___params.html) † | Sets the sample format (`S16_LE`). |
| [`int snd_pcm_hw_params_set_channels(...)`](https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m___h_w___params.html) † | Sets the channel count (2 for capture, 6 for output). |
| [`int snd_pcm_hw_params_set_rate_near(...)`](https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m___h_w___params.html) † | Requests a sample rate close to 48000 Hz. |
| [`int snd_pcm_hw_params_set_period_size_near(...)`](https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m___h_w___params.html) † | Requests a period size close to the target (drives capture/detection granularity). |
| [`int snd_pcm_hw_params_set_buffer_size_near(...)`](https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m___h_w___params.html) † | Requests a buffer size close to the target. |
| [`int snd_pcm_hw_params(snd_pcm_t *pcm, snd_pcm_hw_params_t *params)`](https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m.html#ga1ca0dc120a484965e26cabf966502330) | Applies the configured hardware parameters to the device. |

Docs: <https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m___h_w___params.html>

† These six `_set_*` functions are macro-generated in alsa-lib's source
(`pcm_params.c`), and the current doxygen build doesn't give them individual
anchors — only the parent group page, linked above, actually resolves.
Signatures are correct; only the deep link isn't available.

### Software params (`snd_pcm_sw_params_t`)

Runtime behavior (not hardware config) — specifically start threshold, which
controls when playback begins/resumes after an xrun.

| Function | Description |
|---|---|
| [`snd_pcm_sw_params_alloca(snd_pcm_sw_params_t **ptr)`](https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m___s_w___params.html#ga8e564553bdc89948c918729e3cc7beb0) *(macro)* | Stack-allocates a `sw_params` struct. |
| [`int snd_pcm_sw_params_current(snd_pcm_t *pcm, snd_pcm_sw_params_t *params)`](https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m.html#ga61c5495ffb44c75aaa595e85512d28de) | Fills the struct with the device's current software configuration. |
| [`int snd_pcm_sw_params_set_start_threshold(...)`](https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m___s_w___params.html) † | Sets how many frames must be queued before playback starts/resumes — tuned low here so xrun recovery is fast rather than waiting to refill the whole buffer. |
| [`int snd_pcm_sw_params(snd_pcm_t *pcm, snd_pcm_sw_params_t *params)`](https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m.html#ga891ccaeea2c685a533b61b5fa0493974) | Applies the configured software parameters. |

Docs: <https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m___s_w___params.html>

† Same situation as the hw_params setters above — macro-generated, no
individual anchor in the current build.

---

## Not covered above

`poll()`, `read()`, `signal()`, `sched_setscheduler()`, `clock_gettime()`,
`localtime_r()`, `strftime()` are standard POSIX/C library calls, not
ALSA/FFmpeg APIs, and are omitted from this reference — see `man 2`/`man 3`
for each if needed.