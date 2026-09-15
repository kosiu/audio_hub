// g++ -O2 -Wall -std=c++23 -o toslink_play toslink_play.cpp -lasound -lavformat -lavcodec -lavutil -lswresample
// sudo setcap cap_sys_nice+ep ./toslink_play

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
#include <sched.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cstdarg>
#include <cerrno>
#include <ctime>
#include <vector>

constexpr int AC3_FRAME_SAMPLES            = 1536;  // number of samples per AC3 frame
constexpr int CAP_WAIT_MS                  = 30;    // capture wait time in milliseconds

constexpr int  DECODER_FAIL_COOLDOWN_ITERS = 100; // number of iterations to wait after a decoder failure
constexpr long DECODE_STALL_TIMEOUT_MS     = 100; // timeout in milliseconds for decode stall

constexpr int CAP_CHANNELS = 2;
constexpr int OUT_CHANNELS = 6;
constexpr int CH_REMAP[OUT_CHANNELS] = {0, 1, 4, 5, 2, 3};

constexpr uint8_t AC3_DATA_TYPE{0x01};

static void log_line(const char* fmt, ...) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm_buf;
    localtime_r(&ts.tv_sec, &tm_buf);
    char stamp[32];
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm_buf);
    std::fprintf(stderr, "%s.%03ld ", stamp, ts.tv_nsec / 1000000);

    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
}

struct Device {
    const char* name;              // device name
    unsigned long period_frames;   // number of frames per period
    unsigned long buffer_frames;   // total number of frames in the buffer 
    snd_pcm_t* id = nullptr;       // ALSA PCM handle
    unsigned int rate = 48000;     // sample rate for the device
    std::vector<int16_t> buf{};    // audio buffer for the device
    uint8_t* raw_buf = nullptr;    // raw audio buffer for the device
    bool write_all(snd_pcm_sframes_t frames) {
        int16_t* buf_ptr = this->buf.data();
        while (frames > 0) {
            snd_pcm_sframes_t written = snd_pcm_writei(id, buf_ptr, frames);
            if (written < 0) {
                written = snd_pcm_recover(id, written, 1);
                if (written < 0) {
                    log_line("playback unrecoverable: %s\n", snd_strerror((int)written));
                    return false;
                }
                continue;
            }
            buf_ptr += written * OUT_CHANNELS;
            frames -= written;
        }
        return true;
    }
    ~Device() {if (id) snd_pcm_close(id);}
    Device(const char* n, snd_pcm_stream_t stream, int channels, unsigned long p, unsigned long b): 
            name(n), period_frames(p), buffer_frames(b) {
        // Open and configure PCM device and start playback or capture
        snd_pcm_hw_params_t *hw;
        int err;

        err = snd_pcm_open(&id, name, stream, 0);
        if (err < 0) {log_line("open %s failed: %s\n", name, snd_strerror(err)); exit(1); }

        snd_pcm_hw_params_alloca(&hw);
        snd_pcm_hw_params_any(id, hw);
        snd_pcm_hw_params_set_access(id, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
        snd_pcm_hw_params_set_format(id, hw, SND_PCM_FORMAT_S16_LE);
        snd_pcm_hw_params_set_channels(id, hw, channels);
        snd_pcm_hw_params_set_rate_near(id, hw, &rate, 0);
        snd_pcm_hw_params_set_period_size_near(id, hw, &period_frames, 0);
        snd_pcm_hw_params_set_buffer_size_near(id, hw, &buffer_frames);

        err = snd_pcm_hw_params(id, hw);
        if (err < 0) {log_line("set hw_params on %s failed: %s\n", name, snd_strerror(err)); exit(1); }

        err = snd_pcm_prepare(id);
        if (err < 0) {log_line("prepare %s failed: %s\n", name, snd_strerror(err)); exit(1); }

        log_line("%s: rate=%u period=%lu buffer=%lu channels=%u\n", name, rate, period_frames,
                    buffer_frames, channels);

        buf.resize(buffer_frames * channels);
        raw_buf = reinterpret_cast<uint8_t*>(buf.data());

        if (stream == SND_PCM_STREAM_CAPTURE) {       // Capture stream, start immediately
            int err = snd_pcm_start(id);
            if (err < 0) { log_line("capture start failed: %s\n", snd_strerror(err)); exit(1); }
            return;
        }

        unsigned long delay = period_frames * 2;   // Wait for two periods before starting playback
        snd_pcm_sw_params_t* sw;
        snd_pcm_sw_params_alloca(&sw);
        snd_pcm_sw_params_current(id, sw);
        snd_pcm_sw_params_set_start_threshold(id, sw, delay);
        err = snd_pcm_sw_params(id, sw);
        if (err < 0) {log_line("%s: set delay failed: %s\n", name, snd_strerror(err)); exit(1);}
        log_line("%s: wait=%lu frames (~%.1fms)\n", name, delay, 1000.0 * delay / rate);
    }
};

Device cap("hw:CARD=ICUSBAUDIO7D,DEV=0", SND_PCM_STREAM_CAPTURE, 2, 240, 240*20); // 240 frames -> 5ms @ 48kHz
Device out("Surround", SND_PCM_STREAM_PLAYBACK, 6, 240, AC3_FRAME_SAMPLES * 2);

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

static void check_stdin() {
    struct pollfd pfd{ STDIN_FILENO, POLLIN, 0 };
    if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
        char buf[64];
        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
        if (n > 0) state.print();
    }
}

// FFmpeg decoder context and related structures
static AVFormatContext* fmt_ctx   = nullptr;
static AVIOContext*     avio_ctx  = nullptr;
static AVCodecContext*  codec_ctx = nullptr;
static SwrContext*      swr_ctx   = nullptr;
static AVPacket*        dec_pkt   = nullptr;
static AVFrame*         dec_frame = nullptr;

static struct timespec last_frame_time;
static long elapsed_ms(const struct timespec& from) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - from.tv_sec) * 1000 + (now.tv_nsec - from.tv_nsec) / 1000000;
}

static int avio_read_cb(void*, uint8_t* buf, int buf_size) {
    if (elapsed_ms(last_frame_time) > DECODE_STALL_TIMEOUT_MS) {
        return AVERROR_EOF;
    }

    int frames_wanted = buf_size / (CAP_CHANNELS * (int)sizeof(int16_t));
    if (frames_wanted <= 0) return 0;

    int ready = snd_pcm_wait(cap.id, CAP_WAIT_MS);
    if (ready <= 0) return AVERROR_EOF;

    snd_pcm_sframes_t frames = snd_pcm_readi(cap.id, buf, frames_wanted);
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
        log_line("decoder failing repeatedly, cooling down\n");
        decoder_fail_streak = 0;
    }
}

static bool open_decoder() {
    close_decoder();
    clock_gettime(CLOCK_MONOTONIC, &last_frame_time);

    constexpr int AVIO_BUF_SIZE = 4096;
    uint8_t* avio_buf = (uint8_t*)av_malloc(AVIO_BUF_SIZE);
    avio_ctx = avio_alloc_context(avio_buf, AVIO_BUF_SIZE, 0, nullptr, &avio_read_cb, nullptr, nullptr);
    if (!avio_ctx) { log_line("avio_alloc_context failed\n"); return false; }

    fmt_ctx = avformat_alloc_context();
    fmt_ctx->pb = avio_ctx;
    fmt_ctx->probesize = 8192;
    fmt_ctx->max_analyze_duration = 0;

    AVInputFormat* infmt = av_find_input_format("spdif");
    if (!infmt) { log_line("spdif demuxer not available\n"); close_decoder(); return false; }

    if (avformat_open_input(&fmt_ctx, nullptr, infmt, nullptr) < 0) {
        log_line("avformat_open_input failed\n");
        close_decoder();
        return false;
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        log_line("avformat_find_stream_info failed\n");
        close_decoder();
        return false;
    }

    if (fmt_ctx->nb_streams < 1) {
        log_line("spdif: no stream found\n");
        close_decoder();
        return false;
    }

    AVCodecParameters* params = fmt_ctx->streams[0]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(params->codec_id);
    if (!codec) {
        log_line("no decoder for codec_id=%d\n", params->codec_id);
        close_decoder();
        return false;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, params);

    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        log_line("avcodec_open2 failed\n");
        close_decoder();
        return false;
    }

    log_line("decoder opened: codec_id=%d channels=%d rate=%d\n",
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
        log_line("avcodec_send_packet: %s (skipping)\n", eb);
        return true;
    }

    while (true) {
        ret = avcodec_receive_frame(codec_ctx, dec_frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) {
            char eb[64]; av_strerror(ret, eb, sizeof(eb));
            log_line("avcodec_receive_frame: %s (skipping block)\n", eb);
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
                log_line("swr_init failed\n");
                return false;
            }
        }
        constexpr int DECODE_SCRATCH_FRAMES = AC3_FRAME_SAMPLES + 512; // extra frames for decoding scratch space
        static int16_t swr_buf[DECODE_SCRATCH_FRAMES * OUT_CHANNELS];
        uint8_t* out_ptrs[1] = { (uint8_t*)swr_buf };
        int converted = swr_convert(swr_ctx, out_ptrs, DECODE_SCRATCH_FRAMES,
                                     (const uint8_t**)dec_frame->extended_data,
                                     dec_frame->nb_samples);
        if (converted < 0) { log_line("swr_convert failed\n"); return false; }

        for (int i = 0; i < converted; i++)
            for (int c = 0; c < OUT_CHANNELS; c++)
                out.buf[i * OUT_CHANNELS + c] = swr_buf[i * OUT_CHANNELS + CH_REMAP[c]];

        if (!out.write_all(converted)) return false;
    }
    return true;
}

int main() {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    struct sched_param sp{.sched_priority = 10}; // Set the realtime priority to 10
    if (sched_setscheduler(0, SCHED_FIFO, &sp) != 0) {
        log_line("note: couldn't set realtime priority (%s), continuing with normal\n", strerror(errno));
    } else {
        log_line("realtime scheduling enabled (SCHED_FIFO, priority %d)\n", sp.sched_priority);
    }

    unsigned long xrun_count = 0;
    bool decoding = false;
    int decoder_fail_streak = 0;
    int decoder_cooldown = 0;

    snd_pcm_reset(cap.id); // to have clean buffer (reduce delay)
    while (running) {
        check_stdin();

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

        int ready = snd_pcm_wait(cap.id, CAP_WAIT_MS);
        if (ready == 0) { state.set(State::Status::off); continue; }
        if (ready < 0) { log_line("capture wait error: %s\n", snd_strerror(ready)); exit(1); } // xrun is recoverable
        
        snd_pcm_sframes_t frames = snd_pcm_readi(cap.id, cap.buf.data(), cap.period_frames);

        long preamble_index = -1; // index of preamble of IEC 61937
        bool silent = true;
        for (long i = 0; i + 2 <= frames * CAP_CHANNELS; i += 2) {
            if (cap.buf[i] != 0 || cap.buf[i+1] != 0) silent = false;
            if (cap.buf[i] == (int16_t)0xf872 && (int16_t)cap.buf[i+1] == 0x4e1f) {
                preamble_index = i;
                break;
            }
        }
        if      (preamble_index != -1) state.set(State::ac3);
        else if (silent)               state.set(State::none);
        else                           state.set(State::pcm);

        if (state.current == State::ac3) {
            if (decoder_cooldown > 0) {
                decoder_cooldown--;
                std::fill(out.buf.begin(), out.buf.end(), 0);
                out.write_all(frames);
            } else if (open_decoder()) {
                decoding = true;
                continue;
            } else {
                note_decoder_failure(decoder_fail_streak, decoder_cooldown);
                std::fill(out.buf.begin(), out.buf.end(), 0);
                out.write_all(frames);
            }
        } else if (state.current == State::pcm) {
            for (snd_pcm_sframes_t i = 0; i < frames; i++) {
                int16_t l = cap.buf[i * CAP_CHANNELS + 0];
                int16_t r = cap.buf[i * CAP_CHANNELS + 1];
                out.buf[i * OUT_CHANNELS + 0] = l;
                out.buf[i * OUT_CHANNELS + 1] = r;
                out.buf[i * OUT_CHANNELS + 2] = l;
                out.buf[i * OUT_CHANNELS + 3] = r;
                out.buf[i * OUT_CHANNELS + 4] = l / 2 + r / 2;
                out.buf[i * OUT_CHANNELS + 5] = 0;
            }
            out.write_all(frames);
        } else {
            std::fill(out.buf.begin(), out.buf.end(), 0);
            out.write_all(frames);
        }   
    }

    log_line("shutting down, xrun_count=%lu\n", xrun_count);
    close_decoder();
    return 0;
}
