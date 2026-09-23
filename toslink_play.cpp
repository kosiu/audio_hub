// g++ -O3 -Wall -std=c++23 -o toslink_play toslink_play.cpp -lasound -lavcodec -lavutil -lswresample
extern "C" {
#include <alsa/asoundlib.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
} // most of C standard library was pulled here
#include <vector>

constexpr int AC3_BURST_SAMPLES = 1536;  // samples per AC3 burst period, 32ms @ 48kHz
constexpr int CAP_WAIT_MS       = 30;    // capture wait time in milliseconds, timeout => no light
constexpr long MUTE_BURSTS      = 60;    // ~2s of digital silence before standby (amp stays cool)
constexpr long UNMUTE_BURSTS    = 3;     // ~100ms of real audio before leaving standby (unmute late)
constexpr int sz16 = sizeof(int16_t);
constexpr int CH2 = 2;
constexpr int CH6 = 6;
constexpr int CH_REMAP[CH6] = {0, 1, 4, 5, 2, 3}; // ffmpeg FL FR FC LFE BL BR -> ALSA FL FR RL RR C LFE

struct Amplifier {
    bool standby_state = true;   // start muted and say so before any sound can reach the speakers
    long quiet = MUTE_BURSTS;
    long loud = 0;
    Amplifier() { print(); }
    ~Amplifier() { stb(true); }
    void print() {fputs(standby_state ? "off\n" : "on\n", stdout); fflush(stdout);}
    void stb(bool standby) {if (standby_state != standby) {standby_state = standby; print();} }
    void mute() { stb(true); quiet = MUTE_BURSTS; loud = 0;}
    void observe(const int16_t* samples, long count) {
        while (count-- > 0) if (*samples++) {
            quiet = 0;
            if (++loud >= UNMUTE_BURSTS) stb(false);
            return;
        }
        loud = 0;
        if (++quiet >= MUTE_BURSTS) stb(true);
    }
} amplifier;

static void log_line(const char* fmt, ...) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm_buf;
    localtime_r(&ts.tv_sec, &tm_buf);
    char stamp[32];
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm_buf);
    fprintf(stderr, "%s.%03ld ", stamp, ts.tv_nsec / 1000000);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

static bool check_stdin() {
    struct pollfd pfd{ STDIN_FILENO, POLLIN, 0 };
    if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
        char buf[64];
        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
        if (n > 0 && buf[0] == 'q') return false;
    }
    return true;
}

struct Device {
    const char* name;              // device name
    unsigned long period_frames;   // number of frames per period
    unsigned long buffer_frames;   // total number of frames in the buffer
    snd_pcm_t* id = nullptr;       // ALSA PCM handle
    unsigned int rate = 48000;     // sample rate for the device
    std::vector<int16_t> buf{};    // audio buffer for the device
    bool write_all(snd_pcm_sframes_t frames) {
        int16_t* write_at = buf.data();
        while (frames > 0) {
            snd_pcm_sframes_t written = snd_pcm_writei(id, write_at, frames);
            if (written < 0) {
                int recovered = snd_pcm_recover(id, (int)written, 1);
                if (recovered < 0) {
                    log_line("playback unrecoverable: %s\n", snd_strerror(recovered));
                    return false;
                }
                continue;
            }
            if (written == 0) { log_line("playback wrote no frames\n"); return false; }
            write_at += written * CH6;
            frames -= written;
        }
        return true;
    }
    long read_wait(long at) {
        int ready = snd_pcm_wait(id, CAP_WAIT_MS);
        if (ready == 0) return -1;
        if (ready < 0) return snd_pcm_recover(id, ready, 1) < 0 ? -2 : 0;
        long room = ((long)buf.size() - at) / CH2;         // never read past the end of buf
        if (room > (long)period_frames) room = period_frames;
        if (room <= 0) return -2;
        snd_pcm_sframes_t n = snd_pcm_readi(id, &buf[at], room);
        if (n == -EAGAIN) return 0;
        if (n < 0) return snd_pcm_recover(id, (int)n, 1) < 0 ? -2 : 0;
        return n;
    }
    ~Device() {if (id) snd_pcm_close(id);}
    Device(const char* n, bool capture, unsigned long p, unsigned long b):
            name(n), period_frames(p), buffer_frames(b) {
        // Open and configure PCM device and start capture
        snd_pcm_stream_t stream = capture ? SND_PCM_STREAM_CAPTURE : SND_PCM_STREAM_PLAYBACK;
        int channels            = capture ? CH2 : CH6;
        int mode                = capture ? SND_PCM_NONBLOCK : 0;
        snd_pcm_hw_params_t *hw;
        int err;

        err = snd_pcm_open(&id, name, stream, mode);
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
        if (capture) {                                // Capture stream, start immediately
            err = snd_pcm_start(id);
            if (err < 0) { log_line("capture start failed: %s\n", snd_strerror(err)); exit(1); }
        }
    }
};

Device cap("hw:CARD=ICUSBAUDIO7D,DEV=0", true,  240,               AC3_BURST_SAMPLES * 2);
Device out("Surround",                   false, AC3_BURST_SAMPLES, AC3_BURST_SAMPLES * 2);

struct Ac3Decoder {
    AVCodecContext* codec = nullptr;
    SwrContext* swr = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    ~Ac3Decoder() {
        swr_free(&swr);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codec);
    }
    Ac3Decoder() {
        const AVCodec* ac3 = avcodec_find_decoder(AV_CODEC_ID_AC3);
        if (!ac3) { log_line("AC-3 decoder not available\n"); exit(2); }
        codec = avcodec_alloc_context3(ac3);
        packet = av_packet_alloc();
        frame = av_frame_alloc();
        if (!codec || !packet || !frame || avcodec_open2(codec, ac3, nullptr) < 0) {
            log_line("could not open AC-3 decoder\n");
            exit(2);
        }
    }
    int decode(const uint8_t* payload, size_t payload_bytes) {
        if (av_new_packet(packet, (int)payload_bytes) < 0) {
            log_line("could not allocate AC-3 packet\n");
            return -1;
        }
        swab(payload, packet->data, payload_bytes);
        int ret = avcodec_send_packet(codec, packet);
        av_packet_unref(packet);
        if (ret < 0) {
            char error[64]; av_strerror(ret, error, sizeof(error));
            log_line("AC-3 packet rejected: %s\n", error);
            return -1;
        }
        ret = avcodec_receive_frame(codec, frame);
        if (ret < 0) {
            char error[64]; av_strerror(ret, error, sizeof(error));
            log_line("AC-3 decode failed: %s\n", error);
            return -1;
        }
        if (!swr) {
            int64_t input_layout = frame->channel_layout ? frame->channel_layout :
                av_get_default_channel_layout(frame->channels);
            swr = swr_alloc_set_opts(nullptr,
                AV_CH_LAYOUT_5POINT1, AV_SAMPLE_FMT_S16, frame->sample_rate,
                input_layout, (AVSampleFormat)frame->format, frame->sample_rate, 0, nullptr);
            if (!swr || swr_init(swr) < 0) {
                log_line("could not initialize AC-3 resampler\n");
                return -1;
            }
        }
        uint8_t* output = reinterpret_cast<uint8_t*>(out.buf.data());
        int converted = swr_convert(swr, &output, AC3_BURST_SAMPLES,
            (const uint8_t**)frame->extended_data, frame->nb_samples);
        if (converted < 0) { log_line("could not convert AC-3 frame\n"); return -1; }
        return converted;
    }
} ac3_decoder;

enum class State { none, pcm, ac3, other };  // none = no light, other = a codec we do not decode

int main() {
    snd_pcm_reset(cap.id);      // to have clean buffer (reduce delay)
    long have  = 0;             // samples carried from a capture read that crossed a burst boundary
    State mode = State::none;   // what the last round played, to notice a codec switch

    while (check_stdin()) {
        constexpr long burst_samples = AC3_BURST_SAMPLES * CH2;
        long target   = burst_samples;
        long preamble = -1;
        State state   = State::pcm;
        bool light    = true;

        auto scan = [&](long from, long to) {
            for (long i = from; preamble < 0 && i + 4 <= to; i += 2) {
                if ((uint16_t)cap.buf[i] != 0xf872 || (uint16_t)cap.buf[i + 1] != 0x4e1f) continue;
                long words = ((uint16_t)cap.buf[i + 3] + 15) / 16;  // Pd is the payload size in bits
                words += words & 1;                                 // keep cap.buf frame aligned
                if (words < 2 || i + 4 + words > (long)cap.buf.size() ||
                    i + burst_samples > (long)cap.buf.size()) continue;
                preamble = i;
                target   = i + burst_samples;
                state    = ((uint16_t)cap.buf[i + 2] & 0x7f) == 1 ? State::ac3 : State::other;
            }
        };

        scan(0, have);
        while (have < target) {
            long n = cap.read_wait(have);
            if (n == -2) { log_line("capture lost\n"); return 1; }
            if (n == -1) { light = false; break; }
            if (n == 0) continue;
            long from = have;
            have += n * CH2;
            scan(from > 0 ? from - 2 : 0, have);
        }
        if (!light) { amplifier.mute(); mode = State::none; have = 0; continue; }
        if (state != mode) { amplifier.mute(); mode = state; }

        constexpr long frames = AC3_BURST_SAMPLES;
        if (state == State::pcm) {
            for (long i = 0; i < frames; i++) {
                int16_t l = cap.buf[i * CH2 + 0];
                int16_t r = cap.buf[i * CH2 + 1];
                out.buf[i * CH6 + 0] = l;
                out.buf[i * CH6 + 1] = r;
                out.buf[i * CH6 + 2] = l;
                out.buf[i * CH6 + 3] = r;
                out.buf[i * CH6 + 4] = l / 2 + r / 2;
                out.buf[i * CH6 + 5] = 0;
            }
        } else if (state == State::ac3) {
            int decoded = ac3_decoder.decode((uint8_t*)(cap.buf.data() + preamble + 4),
                                             (uint16_t)cap.buf[preamble + 3] / 8);
            if (decoded != frames) {amplifier.mute(); memset(out.buf.data(), 0, frames*CH6*sz16);}
            else for (int i = 0; i < frames; i++) { // ffmpeg speaker order -> "Surround" order
                int16_t* s = out.buf.data() + i * CH6;
                int16_t t[CH6];
                memcpy(t, s, sizeof(t));
                for (int c = 0; c < CH6; c++) s[c] = t[CH_REMAP[c]];
            }
        } else {                                  // DTS & friends: never send raw data to speakers
            memset(out.buf.data(), 0, frames * CH6 * sz16);
        }

        amplifier.observe(out.buf.data(), frames * CH6);

        if (!out.write_all(frames)) return 1;

        have -= target;
        if (have > 0) memmove(cap.buf.data(), &cap.buf[target], have * sz16);
    }

    log_line("shutting down\n");
    return 0;
}
