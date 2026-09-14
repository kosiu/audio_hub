// g++ -O2 -Wall -std=c++23 -o toslink_play toslink_play.cpp -lasound -lavformat -lavcodec -lavutil -lswresample
// sudo setcap cap_sys_nice+ep ./toslink_play

extern "C" {
#include <alsa/asoundlib.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <signal.h>
#include <sched.h>
#include <unistd.h>
#include <vector>

constexpr int RATE = 48000;
constexpr int CAP_CHANNELS = 2;
constexpr int OUT_CHANNELS = 6;
constexpr snd_pcm_uframes_t PERIOD = 240;   // 5 ms @ 48 kHz
constexpr snd_pcm_uframes_t CAP_BUFFER = 720; // 15 ms
constexpr snd_pcm_uframes_t OUT_BUFFER = 1536; // one AC-3 decoded frame
constexpr int CAP_WAIT_MS = 20;
constexpr size_t DETECT_WINDOW = 8192;
constexpr int AC3_FRAME_SAMPLES = 1536;

constexpr int CH_REMAP[OUT_CHANNELS] = {0, 1, 4, 5, 2, 3};

struct Device {
    const char* name;
    snd_pcm_t* id = nullptr;
    unsigned rate = RATE;
    snd_pcm_uframes_t period = PERIOD;
    snd_pcm_uframes_t buffer = CAP_BUFFER;
    std::vector<int16_t> buf;
};

Device cap{
    .name = "hw:CARD=ICUSBAUDIO7D,DEV=0",
    .rate = RATE,
    .period = PERIOD,
    .buffer = CAP_BUFFER,
};

Device out{
    .name = "Surround",
    .rate = RATE,
    .period = PERIOD,
    .buffer = OUT_BUFFER,
};

static volatile sig_atomic_t running = 1;
static void on_signal(int) { running = 0; }

static void log_line(const char* fmt, ...) {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    tm tm_buf{};
    localtime_r(&ts.tv_sec, &tm_buf);
    char stamp[32];
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm_buf);

    std::fprintf(stderr, "%s.%03ld ", stamp, ts.tv_nsec / 1000000);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
}

[[noreturn]] static void fail(const char* what, int err) {
    log_line("%s: %s\n", what, snd_strerror(err));
    std::exit(1);
}

static void open_pcm(Device& dev, snd_pcm_stream_t stream, int channels) {
    const int flags = stream == SND_PCM_STREAM_CAPTURE ? SND_PCM_NONBLOCK : 0;

    int err = snd_pcm_open(&dev.id, dev.name, stream, flags);
    if (err < 0) fail("snd_pcm_open", err);

    snd_pcm_hw_params_t* hw;
    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(dev.id, hw);
    snd_pcm_hw_params_set_access(dev.id, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(dev.id, hw, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(dev.id, hw, channels);
    snd_pcm_hw_params_set_rate_near(dev.id, hw, &dev.rate, nullptr);
    snd_pcm_hw_params_set_period_size_near(dev.id, hw, &dev.period, nullptr);
    snd_pcm_hw_params_set_buffer_size_near(dev.id, hw, &dev.buffer);

    err = snd_pcm_hw_params(dev.id, hw);
    if (err < 0) fail("snd_pcm_hw_params", err);

    // ALSA may have rounded period/buffer to nearby values.
    snd_pcm_hw_params_get_period_size(hw, &dev.period, nullptr);
    snd_pcm_hw_params_get_buffer_size(hw, &dev.buffer);

    dev.buf.resize(static_cast<size_t>(dev.period) * channels);

    snd_pcm_sw_params_t* sw;
    snd_pcm_sw_params_alloca(&sw);
    snd_pcm_sw_params_current(dev.id, sw);
    snd_pcm_sw_params_set_avail_min(dev.id, sw, dev.period);

    if (stream == SND_PCM_STREAM_PLAYBACK) {
        // Start once one period has been queued.
        snd_pcm_sw_params_set_start_threshold(dev.id, sw, dev.period);
    }

    err = snd_pcm_sw_params(dev.id, sw);
    if (err < 0) fail("snd_pcm_sw_params", err);

    err = snd_pcm_prepare(dev.id);
    if (err < 0) fail("snd_pcm_prepare", err);

    log_line("%s: rate=%u period=%lu buffer=%lu channels=%d mode=%s\n",
             dev.name, dev.rate,
             static_cast<unsigned long>(dev.period),
             static_cast<unsigned long>(dev.buffer),
             channels,
             stream == SND_PCM_STREAM_CAPTURE ? "nonblock" : "block");
}

static bool wait_capture() {
    for (;;) {
        int r = snd_pcm_wait(cap.id, CAP_WAIT_MS);
        if (r > 0)
            return true;
        if (r == 0)
            return false;

        r = snd_pcm_recover(cap.id, r, 1);
        if (r < 0) {
            log_line("capture wait unrecoverable: %s\n", snd_strerror(r));
            return false;
        }
    }
}

static snd_pcm_sframes_t read_capture(int16_t* dst, snd_pcm_uframes_t frames) {
    for (;;) {
        snd_pcm_sframes_t n = snd_pcm_readi(cap.id, dst, frames);
        if (n >= 0)
            return n;
        if (n == -EAGAIN)
            return 0;

        n = snd_pcm_recover(cap.id, static_cast<int>(n), 1);
        if (n < 0) {
            log_line("capture read unrecoverable: %s\n", snd_strerror(static_cast<int>(n)));
            return n;
        }
    }
}

static bool write_all(const int16_t* data, snd_pcm_sframes_t frames) {
    while (frames > 0 && running) {
        snd_pcm_sframes_t n = snd_pcm_writei(out.id, data, frames);
        if (n > 0) {
            data += n * OUT_CHANNELS;
            frames -= n;
            continue;
        }

        if (n == -EAGAIN)
            continue;

        n = snd_pcm_recover(out.id, static_cast<int>(n), 1);
        if (n < 0) {
            log_line("playback unrecoverable: %s\n", snd_strerror(static_cast<int>(n)));
            return false;
        }
    }
    return frames == 0;
}

static void pcm_stereo_to_5_1(const int16_t* in, int16_t* out_buf, snd_pcm_sframes_t frames) {
    for (snd_pcm_sframes_t i = 0; i < frames; ++i) {
        const int16_t l = in[i * CAP_CHANNELS + 0];
        const int16_t r = in[i * CAP_CHANNELS + 1];

        out_buf[i * OUT_CHANNELS + 0] = l;
        out_buf[i * OUT_CHANNELS + 1] = r;
        out_buf[i * OUT_CHANNELS + 2] = static_cast<int16_t>((static_cast<int>(l) + r) / 2);
        out_buf[i * OUT_CHANNELS + 3] = 0;
        out_buf[i * OUT_CHANNELS + 4] = l;
        out_buf[i * OUT_CHANNELS + 5] = r;
    }
}

// Small rolling detector. Keep only a bounded amount of pre-detection data;
// once AC-3 is found, FFmpeg receives that data and continues from there.
struct InputBuffer {
    std::vector<uint8_t> data;

    void append(const uint8_t* p, size_t n) {
        data.insert(data.end(), p, p + n);
        if (data.size() > DETECT_WINDOW)
            data.erase(data.begin(), data.begin() + (data.size() - DETECT_WINDOW));
    }

    bool has_ac3_burst() const {
        constexpr uint8_t preamble[] = {0x72, 0xF8, 0x1F, 0x4E};
        for (size_t i = 0; i + sizeof(preamble) <= data.size(); ++i)
            if (std::memcmp(data.data() + i, preamble, sizeof(preamble)) == 0)
                return true;
        return false;
    }

    void clear() { data.clear(); }
};

static AVFormatContext* fmt_ctx = nullptr;
static AVIOContext* avio_ctx = nullptr;
static AVCodecContext* codec_ctx = nullptr;
static SwrContext* swr_ctx = nullptr;
static AVPacket* dec_pkt = nullptr;
static AVFrame* dec_frame = nullptr;

// Bytes already captured remain here when the AC-3 decoder is opened.
static InputBuffer input_buffer;
static size_t input_pos = 0;
static bool input_timeout = false;

static void compact_input() {
    if (input_pos > 0 && input_pos >= input_buffer.data.size() / 2) {
        input_buffer.data.erase(input_buffer.data.begin(), input_buffer.data.begin() + input_pos);
        input_pos = 0;
    }
}

static bool capture_one_period_to_compressed() {
    if (!wait_capture()) {
        input_timeout = true;
        return false;
    }

    const auto frames = read_capture(cap.buf.data(), cap.period);
    if (frames <= 0)
        return false;

    const auto* p = reinterpret_cast<const uint8_t*>(cap.buf.data());
    const size_t bytes = static_cast<size_t>(frames) * CAP_CHANNELS * sizeof(int16_t);
    input_buffer.append(p, bytes);
    return true;
}

static int avio_read_cb(void*, uint8_t* dst, int dst_size) {
    while (running) {
        const size_t available = input_buffer.data.size() - input_pos;
        if (available > 0) {
            const size_t n = std::min(available, static_cast<size_t>(dst_size));
            std::memcpy(dst, input_buffer.data.data() + input_pos, n);
            input_pos += n;
            compact_input();
            return static_cast<int>(n);
        }

        input_timeout = false;
        if (!capture_one_period_to_compressed())
            return AVERROR_EOF;
    }

    return AVERROR_EXIT;
}

static void close_decoder() {
    av_packet_free(&dec_pkt);
    av_frame_free(&dec_frame);

    avcodec_free_context(&codec_ctx);
    if (fmt_ctx)
        avformat_close_input(&fmt_ctx);
    if (avio_ctx) {
        av_freep(&avio_ctx->buffer);
        avio_context_free(&avio_ctx);
    }
    if (swr_ctx)
        swr_free(&swr_ctx);
}

static bool open_decoder() {
    close_decoder();

    constexpr int AVIO_BUF_SIZE = 4096;
    auto* avio_buf = static_cast<uint8_t*>(av_malloc(AVIO_BUF_SIZE));
    if (!avio_buf)
        return false;

    avio_ctx = avio_alloc_context(
        avio_buf, AVIO_BUF_SIZE,
        0, nullptr,
        &avio_read_cb, nullptr, nullptr);

    if (!avio_ctx) {
        av_free(avio_buf);
        return false;
    }

    fmt_ctx = avformat_alloc_context();
    if (!fmt_ctx)
        return false;

    fmt_ctx->pb = avio_ctx;
    fmt_ctx->probesize = 8192;
    fmt_ctx->max_analyze_duration = 0;

    const AVInputFormat* infmt = av_find_input_format("spdif");
    if (!infmt)
        return false;

    if (avformat_open_input(&fmt_ctx, nullptr, infmt, nullptr) < 0)
        return false;

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0)
        return false;

    if (fmt_ctx->nb_streams < 1)
        return false;

    const AVCodecParameters* params = fmt_ctx->streams[0]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(params->codec_id);
    if (!codec)
        return false;

    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx)
        return false;

    if (avcodec_parameters_to_context(codec_ctx, params) < 0)
        return false;

    if (avcodec_open2(codec_ctx, codec, nullptr) < 0)
        return false;

    log_line("decoder: codec=%d channels=%d rate=%d\n",
             params->codec_id, codec_ctx->ch_layout.nb_channels,
             codec_ctx->sample_rate);
    return true;
}

static bool output_decoded_frame(AVFrame* frame) {
    if (!swr_ctx) {
        AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_5POINT1;
        swr_ctx = swr_alloc_set_opts2(
            nullptr,
            &out_layout, AV_SAMPLE_FMT_S16, frame->sample_rate,
            &frame->ch_layout, static_cast<AVSampleFormat>(frame->format), frame->sample_rate,
            0, nullptr);

        if (!swr_ctx || swr_init(swr_ctx) < 0) {
            log_line("swr init failed\n");
            return false;
        }
    }

    constexpr int MAX_OUT_FRAMES = AC3_FRAME_SAMPLES + 256;
    static std::array<int16_t, MAX_OUT_FRAMES * OUT_CHANNELS> swr_buf;
    static std::array<int16_t, MAX_OUT_FRAMES * OUT_CHANNELS> remap_buf;

    uint8_t* out_ptrs[] = {reinterpret_cast<uint8_t*>(swr_buf.data())};
    const int converted = swr_convert(
        swr_ctx,
        out_ptrs, MAX_OUT_FRAMES,
        const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);

    if (converted < 0)
        return false;

    for (int i = 0; i < converted; ++i) {
        for (int c = 0; c < OUT_CHANNELS; ++c)
            remap_buf[i * OUT_CHANNELS + c] = swr_buf[i * OUT_CHANNELS + CH_REMAP[c]];
    }

    return write_all(remap_buf.data(), converted);
}

static bool decode_available() {
    if (!dec_pkt) dec_pkt = av_packet_alloc();
    if (!dec_frame) dec_frame = av_frame_alloc();

    const int ret = av_read_frame(fmt_ctx, dec_pkt);
    if (ret < 0)
        return false;

    int r = avcodec_send_packet(codec_ctx, dec_pkt);
    av_packet_unref(dec_pkt);
    if (r < 0)
        return true; // skip malformed packet and continue

    while (running) {
        r = avcodec_receive_frame(codec_ctx, dec_frame);
        if (r == AVERROR(EAGAIN) || r == AVERROR_EOF)
            return true;
        if (r < 0)
            return false;

        if (!output_decoded_frame(dec_frame))
            return false;
    }

    return true;
}

static void reset_input_detection() {
    input_buffer.clear();
    input_pos = 0;
    input_timeout = false;
}

int main() {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    sched_param sp{.sched_priority = 10};
    if (sched_setscheduler(0, SCHED_FIFO, &sp) == 0)
        log_line("SCHED_FIFO priority %d\n", sp.sched_priority);

    open_pcm(cap, SND_PCM_STREAM_CAPTURE, CAP_CHANNELS);
    open_pcm(out, SND_PCM_STREAM_PLAYBACK, OUT_CHANNELS);

    bool ac3 = false;
    bool have_decoder = false;

    while (running) {
        if (!ac3) {
            if (!wait_capture()) {
                log_line("capture timeout\n");
                continue;
            }

            const auto frames = read_capture(cap.buf.data(), cap.period);
            if (frames <= 0)
                continue;

            const auto* bytes = reinterpret_cast<const uint8_t*>(cap.buf.data());
            const size_t byte_count = static_cast<size_t>(frames) * CAP_CHANNELS * sizeof(int16_t);

            // Keep what we just captured so opening the decoder does not lose
            // the burst that triggered detection.
            input_buffer.append(bytes, byte_count);

            if (input_buffer.has_ac3_burst()) {
                ac3 = true;
                have_decoder = open_decoder();
                if (!have_decoder) {
                    log_line("AC-3 decoder open failed\n");
                    ac3 = false;
                    reset_input_detection();
                }
                continue;
            }

            // Before an AC-3 burst is found, treat input as ordinary 2ch PCM.
            pcm_stereo_to_5_1(cap.buf.data(), out.buf.data(), frames);
            write_all(out.buf.data(), frames);
            continue;
        }

        if (!have_decoder) {
            ac3 = false;
            continue;
        }

        if (!decode_available()) {
            log_line("AC-3 stream ended/reset%s\n", input_timeout ? " (capture timeout)" : "");
            close_decoder();
            have_decoder = false;
            ac3 = false;
            reset_input_detection();
        }
    }

    close_decoder();
    if (cap.id) snd_pcm_close(cap.id);
    if (out.id) snd_pcm_close(out.id);
    return 0;
}
