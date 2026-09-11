// g++ -O2 -Wall -std=c++23 -o audio_bridge audio_bridge.cpp -lasound -lavformat -lavcodec -lavutil -lswresample

extern "C" {
#include <alsa/asoundlib.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}
#include <poll.h>
#include <unistd.h>
#include <signal.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <ctime>

constexpr const char* CAP_DEVICE  = "hw:CARD=ICUSBAUDIO7D,DEV=0";
constexpr const char* OUT_DEVICE  = "Surround";

constexpr unsigned int RATE       = 48000;
constexpr int CAP_CHANNELS        = 2;
constexpr int OUT_CHANNELS        = 6;
constexpr int CAP_PERIOD_FRAMES   = 240;   // 5ms @ 48kHz
constexpr int CAP_BUFFER_FRAMES   = CAP_PERIOD_FRAMES * 8;
constexpr int OUT_PERIOD_FRAMES   = CAP_PERIOD_FRAMES;

// AC3 frames are always exactly 1536 samples, fixed by spec regardless of
// bitrate. Output buffer needs to absorb one full burst write plus margin,
// or the buffer chronically underruns right after each burst (see history).
constexpr int AC3_FRAME_SAMPLES     = 1536;
constexpr int OUT_BUFFER_FRAMES     = AC3_FRAME_SAMPLES * 2;   // 3072: one burst + equal margin
constexpr int DECODE_SCRATCH_FRAMES = AC3_FRAME_SAMPLES + 512; // headroom for swr_convert scratch buffers

constexpr int CAP_WAIT_MS         = 100;

// One IEC 61937 slot at 48kHz/2ch/16bit is 6144 bytes - window must exceed
// that so a sync word is never missed at a read boundary.
constexpr size_t SCAN_WINDOW = 12288;

constexpr int DECODER_FAIL_COOLDOWN_ITERS = 100;
constexpr long DECODE_STALL_TIMEOUT_MS = 100;   // no decoded frame in this long -> stream genuinely stopped
constexpr int CAP_XRUN_MAX_CONSECUTIVE = 10;    // give up on capture device after this many failed recoveries

static volatile sig_atomic_t running = 1;
static void on_signal(int) { running = 0; }

struct State {
    enum class Status { off, none, pcm, ac3 } current{Status::off};
    using enum Status;
    [[nodiscard]] constexpr const char* str() const {
        switch (current) {
            case off:  return "off";
            case none: return "none";
            case pcm:  return "pcm";
            case ac3:  return "ac3";
        }
        __builtin_unreachable();
    }
    void print() const { std::fputs(str(), stdout); std::fputc('\n', stdout); std::fflush(stdout); }
    void set(Status s) { if (s != current) { current = s; print(); } }
} state;

static snd_pcm_t* cap = nullptr;
static snd_pcm_t* out = nullptr;

#define SND_ERR(format) if(err < 0){fprintf(stderr,format,name,snd_strerror(err));exit(1);}
static snd_pcm_t *open_pcm(const char *name, snd_pcm_stream_t stream, unsigned int channels,
                            int period_frames, int buffer_frames) {
    snd_pcm_t *handle;
    snd_pcm_hw_params_t *hw;
    int err;

    err = snd_pcm_open(&handle, name, stream, 0);
    SND_ERR("open %s failed: %s\n");

    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(handle, hw);
    snd_pcm_hw_params_set_access(handle, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(handle, hw, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(handle, hw, channels);

    auto rate = RATE;
    snd_pcm_hw_params_set_rate_near(handle, hw, &rate, 0);

    snd_pcm_uframes_t period = period_frames;
    snd_pcm_hw_params_set_period_size_near(handle, hw, &period, 0);

    snd_pcm_uframes_t buffer = buffer_frames;
    snd_pcm_hw_params_set_buffer_size_near(handle, hw, &buffer);

    err = snd_pcm_hw_params(handle, hw);
    SND_ERR("set hw_params on %s failed: %s\n");

    fprintf(stderr, "%s: rate=%u period=%lu buffer=%lu channels=%u\n",
                    name, rate, period, buffer, channels);

    err = snd_pcm_prepare(handle);
    SND_ERR("prepare %s failed: %s\n");

    return handle;
}

static void tune_start_threshold(snd_pcm_t* handle, const char* name, snd_pcm_uframes_t threshold) {
    snd_pcm_sw_params_t* sw;
    snd_pcm_sw_params_alloca(&sw);
    snd_pcm_sw_params_current(handle, sw);
    snd_pcm_sw_params_set_start_threshold(handle, sw, threshold);
    int err = snd_pcm_sw_params(handle, sw);
    if (err < 0) {
        std::fprintf(stderr, "%s: sw_params start_threshold failed: %s\n", name, snd_strerror(err));
        return;
    }
    std::fprintf(stderr, "%s: start_threshold=%lu frames (~%.1fms)\n",
                 name, (unsigned long)threshold, 1000.0 * threshold / RATE);
}

static void poll_stdin() {
    struct pollfd pfd{ STDIN_FILENO, POLLIN, 0 };
    if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
        char buf[64];
        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
        if (n > 0) state.print();
    }
}

static struct timespec last_frame_time;
static long elapsed_ms(const struct timespec& from) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - from.tv_sec) * 1000 + (now.tv_nsec - from.tv_nsec) / 1000000;
}

static bool write_all(snd_pcm_t* pcm_handle, const int16_t* data, snd_pcm_sframes_t frames,
                       int channels, unsigned long& xrun_count) {
    while (frames > 0) {
        snd_pcm_sframes_t written = snd_pcm_writei(pcm_handle, data, frames);
        if (written < 0) {
            xrun_count++;
            written = snd_pcm_recover(pcm_handle, written, 1);
            if (written < 0) {
                std::fprintf(stderr, "playback unrecoverable: %s\n", snd_strerror((int)written));
                return false;
            }
            continue;
        }
        data += written * channels;
        frames -= written;
    }
    return true;
}

// --- Bitstream sync detection (AC3 only; DTS support removed) ---
// Still recognizes *any* valid IEC 61937 sync word, not just AC3's - a
// recognized-but-non-AC3 burst (e.g. DTS) is reported as "other" and the
// caller mutes rather than passing the raw compressed bytes through as if
// they were PCM (which would produce harsh noise, same failure mode as
// the very first problem in this project).
enum class Sync { none, ac3, other };

static uint8_t scan_buf[SCAN_WINDOW];
static size_t  scan_len = 0;
static const uint8_t PREAMBLE[4] = {0x72, 0xF8, 0x1F, 0x4E};
constexpr uint8_t AC3_DATA_TYPE = 0x01;

static Sync scan_bitstream(const uint8_t* data, size_t len) {
    if (len >= SCAN_WINDOW) {
        std::memcpy(scan_buf, data + (len - SCAN_WINDOW), SCAN_WINDOW);
        scan_len = SCAN_WINDOW;
    } else {
        size_t total = scan_len + len;
        if (total > SCAN_WINDOW) {
            size_t drop = total - SCAN_WINDOW;
            std::memmove(scan_buf, scan_buf + drop, scan_len - drop);
            scan_len -= drop;
        }
        std::memcpy(scan_buf + scan_len, data, len);
        scan_len += len;
    }
    for (size_t i = 0; i + 6 <= scan_len; i++) {
        if (scan_buf[i] == PREAMBLE[0] && scan_buf[i+1] == PREAMBLE[1] &&
            scan_buf[i+2] == PREAMBLE[2] && scan_buf[i+3] == PREAMBLE[3]) {
            uint16_t pc = scan_buf[i+4] | (scan_buf[i+5] << 8);
            uint8_t data_type = pc & 0x7F;
            return (data_type == AC3_DATA_TYPE) ? Sync::ac3 : Sync::other;
        }
    }
    return Sync::none;
}

// --- Stream Decoder ---
static AVFormatContext* fmt_ctx = nullptr;
static AVIOContext*     avio_ctx = nullptr;
static AVCodecContext*  codec_ctx = nullptr;
static SwrContext*      swr_ctx = nullptr;
static AVPacket*        dec_pkt = nullptr;
static AVFrame*         dec_frame = nullptr;

// Channel order out of the decoder already matched physical output wiring
// on this rig, confirmed by direct listening test against this binary's
// own output. Kept as an explicit table (not collapsed away) in case
// wiring/routing ever changes.
static const int CH_REMAP[OUT_CHANNELS] = {0, 1, 2, 3, 4, 5};

static int avio_read_cb(void*, uint8_t* buf, int buf_size) {
    if (elapsed_ms(last_frame_time) > DECODE_STALL_TIMEOUT_MS) {
        return AVERROR_EOF;
    }

    int frames_wanted = buf_size / (CAP_CHANNELS * (int)sizeof(int16_t));
    if (frames_wanted <= 0) return 0;

    int ready = snd_pcm_wait(cap, CAP_WAIT_MS);
    if (ready <= 0) return AVERROR_EOF;

    snd_pcm_sframes_t frames = snd_pcm_readi(cap, buf, frames_wanted);
    if (frames < 0) return AVERROR(EIO);
    return (int)(frames * CAP_CHANNELS * sizeof(int16_t));
}

static void close_decoder() {
    if (codec_ctx) avcodec_free_context(&codec_ctx);
    if (fmt_ctx)   avformat_close_input(&fmt_ctx);
    if (avio_ctx) {
        av_freep(&avio_ctx->buffer);
        avio_context_free(&avio_ctx);
    }
    if (swr_ctx) swr_free(&swr_ctx);
}

static void note_decoder_failure(int& decoder_fail_streak, int& decoder_cooldown) {
    decoder_fail_streak++;
    if (decoder_fail_streak >= 3) {
        decoder_cooldown = DECODER_FAIL_COOLDOWN_ITERS;
        std::fprintf(stderr, "decoder failing repeatedly, cooling down\n");
        decoder_fail_streak = 0;
    }
}

static bool open_decoder() {
    close_decoder();
    clock_gettime(CLOCK_MONOTONIC, &last_frame_time);   // must precede any read_cb calls

    constexpr int AVIO_BUF_SIZE = 4096;
    uint8_t* avio_buf = (uint8_t*)av_malloc(AVIO_BUF_SIZE);
    avio_ctx = avio_alloc_context(avio_buf, AVIO_BUF_SIZE, 0, nullptr, &avio_read_cb, nullptr, nullptr);
    if (!avio_ctx) { std::fprintf(stderr, "avio_alloc_context failed\n"); return false; }

    fmt_ctx = avformat_alloc_context();
    fmt_ctx->pb = avio_ctx;
    fmt_ctx->probesize = 8192;
    fmt_ctx->max_analyze_duration = 0;

    AVInputFormat* infmt = av_find_input_format("spdif");
    if (!infmt) { std::fprintf(stderr, "spdif demuxer not available\n"); close_decoder(); return false; }

    if (avformat_open_input(&fmt_ctx, nullptr, infmt, nullptr) < 0) {
        std::fprintf(stderr, "avformat_open_input failed\n");
        close_decoder();
        return false;
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        std::fprintf(stderr, "avformat_find_stream_info failed\n");
        close_decoder();
        return false;
    }

    if (fmt_ctx->nb_streams < 1) {
        std::fprintf(stderr, "spdif: no stream found\n");
        close_decoder();
        return false;
    }

    AVCodecParameters* params = fmt_ctx->streams[0]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(params->codec_id);
    if (!codec) {
        std::fprintf(stderr, "no decoder for codec_id=%d\n", params->codec_id);
        close_decoder();
        return false;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, params);

    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        std::fprintf(stderr, "avcodec_open2 failed\n");
        close_decoder();
        return false;
    }

    std::fprintf(stderr, "decoder opened: codec_id=%d channels=%d rate=%d\n",
                 params->codec_id, codec_ctx->channels, codec_ctx->sample_rate);
    return true;
}

static bool decode_and_write_one(unsigned long& xrun_count, bool& produced_frame) {
    if (!dec_pkt) dec_pkt = av_packet_alloc();
    if (!dec_frame) dec_frame = av_frame_alloc();

    int ret = av_read_frame(fmt_ctx, dec_pkt);
    if (ret < 0) { av_packet_unref(dec_pkt); return false; }

    ret = avcodec_send_packet(codec_ctx, dec_pkt);
    av_packet_unref(dec_pkt);
    if (ret < 0) {
        char eb[64]; av_strerror(ret, eb, sizeof(eb));
        std::fprintf(stderr, "avcodec_send_packet: %s (skipping)\n", eb);
        return true;
    }

    while (true) {
        ret = avcodec_receive_frame(codec_ctx, dec_frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) {
            char eb[64]; av_strerror(ret, eb, sizeof(eb));
            std::fprintf(stderr, "avcodec_receive_frame: %s (skipping block)\n", eb);
            break;
        }
        produced_frame = true;
        clock_gettime(CLOCK_MONOTONIC, &last_frame_time);

        if (!swr_ctx) {
            int64_t in_layout = dec_frame->channel_layout ?
                (int64_t)dec_frame->channel_layout : av_get_default_channel_layout(dec_frame->channels);
            swr_ctx = swr_alloc_set_opts(nullptr,
                AV_CH_LAYOUT_5POINT1, AV_SAMPLE_FMT_S16, dec_frame->sample_rate,
                in_layout, (AVSampleFormat)dec_frame->format, dec_frame->sample_rate,
                0, nullptr);
            if (!swr_ctx || swr_init(swr_ctx) < 0) {
                std::fprintf(stderr, "swr_init failed\n");
                return false;
            }
        }

        static int16_t swr_buf[DECODE_SCRATCH_FRAMES * OUT_CHANNELS];
        uint8_t* out_ptrs[1] = { (uint8_t*)swr_buf };
        int converted = swr_convert(swr_ctx, out_ptrs, DECODE_SCRATCH_FRAMES,
                                     (const uint8_t**)dec_frame->extended_data,
                                     dec_frame->nb_samples);
        if (converted < 0) { std::fprintf(stderr, "swr_convert failed\n"); return false; }

        static int16_t remap_buf[DECODE_SCRATCH_FRAMES * OUT_CHANNELS];
        for (int i = 0; i < converted; i++)
            for (int c = 0; c < OUT_CHANNELS; c++)
                remap_buf[i * OUT_CHANNELS + c] = swr_buf[i * OUT_CHANNELS + CH_REMAP[c]];

        if (!write_all(out, remap_buf, converted, OUT_CHANNELS, xrun_count))
            return false;
    }
    return true;
}

int main() {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    out = open_pcm(OUT_DEVICE, SND_PCM_STREAM_PLAYBACK, OUT_CHANNELS, OUT_PERIOD_FRAMES, OUT_BUFFER_FRAMES);
    tune_start_threshold(out, OUT_DEVICE, OUT_PERIOD_FRAMES * 2);
    cap = open_pcm(CAP_DEVICE, SND_PCM_STREAM_CAPTURE, CAP_CHANNELS, CAP_PERIOD_FRAMES, CAP_BUFFER_FRAMES);

    int err = snd_pcm_start(cap);
    if (err < 0) { std::fprintf(stderr, "capture start failed: %s\n", snd_strerror(err)); return 1; }

    int16_t cap_buf[CAP_PERIOD_FRAMES * CAP_CHANNELS];
    int16_t out_buf[CAP_PERIOD_FRAMES * OUT_CHANNELS];

    unsigned long xrun_count = 0;
    unsigned long last_reported_xruns = 0;
    bool decoding = false;
    int decoder_fail_streak = 0;
    int decoder_cooldown = 0;
    int cap_xrun_streak = 0;

    while (running) {
        poll_stdin();

        if (xrun_count != last_reported_xruns) {
            std::fprintf(stderr, "xrun_count=%lu\n", xrun_count);
            last_reported_xruns = xrun_count;
        }

        if (decoding) {
            bool produced = false;
            bool ok = decode_and_write_one(xrun_count, produced);
            if (produced) decoder_fail_streak = 0;
            if (!ok) {
                close_decoder();
                decoding = false;
                if (!produced) note_decoder_failure(decoder_fail_streak, decoder_cooldown);
            }
            continue;
        }

        int ready = snd_pcm_wait(cap, CAP_WAIT_MS);
        if (ready == 0) { state.set(State::off); continue; }
        if (ready < 0) { std::fprintf(stderr, "capture wait error: %s\n", snd_strerror(ready)); continue; }

        snd_pcm_sframes_t frames = snd_pcm_readi(cap, cap_buf, CAP_PERIOD_FRAMES);
        if (frames < 0) {
            std::fprintf(stderr, "capture read error: %s (recovering)\n", snd_strerror((int)frames));
            int rec = snd_pcm_recover(cap, (int)frames, 1);
            if (rec < 0) {
                if (++cap_xrun_streak >= CAP_XRUN_MAX_CONSECUTIVE) {
                    std::fprintf(stderr, "capture unrecoverable after %d attempts, giving up\n", cap_xrun_streak);
                    break;
                }
            } else {
                cap_xrun_streak = 0;
            }
            continue;
        }
        cap_xrun_streak = 0;

        bool silent = true;
        for (snd_pcm_sframes_t i = 0; i < frames * CAP_CHANNELS; i++)
            if (cap_buf[i] != 0) { silent = false; break; }

        Sync sync = scan_bitstream(reinterpret_cast<uint8_t*>(cap_buf),
                                    frames * CAP_CHANNELS * sizeof(int16_t));

        State::Status detected;
        if (sync == Sync::ac3)        detected = State::ac3;
        else if (sync == Sync::other) detected = State::none;  // recognized non-AC3 burst - mute, don't fake-pass as PCM
        else                          detected = silent ? State::none : State::pcm;
        state.set(detected);

        if (state.current == State::ac3) {
            if (decoder_cooldown > 0) {
                decoder_cooldown--;
                std::memset(out_buf, 0, sizeof(out_buf));
                write_all(out, out_buf, frames, OUT_CHANNELS, xrun_count);
            } else if (open_decoder()) {
                decoding = true;
                continue;
            } else {
                note_decoder_failure(decoder_fail_streak, decoder_cooldown);
                std::memset(out_buf, 0, sizeof(out_buf));
                write_all(out, out_buf, frames, OUT_CHANNELS, xrun_count);
            }
        } else if (state.current == State::pcm) {
            for (snd_pcm_sframes_t i = 0; i < frames; i++) {
                int16_t l = cap_buf[i * CAP_CHANNELS + 0];
                int16_t r = cap_buf[i * CAP_CHANNELS + 1];
                out_buf[i * OUT_CHANNELS + 0] = l;
                out_buf[i * OUT_CHANNELS + 1] = r;
                out_buf[i * OUT_CHANNELS + 2] = l / 2 + r / 2;
                out_buf[i * OUT_CHANNELS + 3] = 0;
                out_buf[i * OUT_CHANNELS + 4] = l;
                out_buf[i * OUT_CHANNELS + 5] = r;
            }
            write_all(out, out_buf, frames, OUT_CHANNELS, xrun_count);
        } else {
            std::memset(out_buf, 0, sizeof(out_buf));
            write_all(out, out_buf, frames, OUT_CHANNELS, xrun_count);
        }
    }

    std::fprintf(stderr, "shutting down, xrun_count=%lu\n", xrun_count);
    close_decoder();
    if (cap) snd_pcm_close(cap);
    if (out) snd_pcm_close(out);
    return 0;
}
