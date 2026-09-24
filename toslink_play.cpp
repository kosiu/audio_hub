// reminder sound speed: 3 ms/m
// g++ -O3 -Wall -std=c++23 -o toslink_play toslink_play.cpp -lasound -lavcodec -lavutil -lswresample
extern "C" {
#include <alsa/asoundlib.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
} // most of C standard library was pulled here
#include <vector>
using std::vector, std::min, std::max, std::swap_ranges;

constexpr int AC3_BURST = 1536; // samples per AC3 burst period, 32ms @ 48kHz (main unit for sync)
constexpr int sz16 = sizeof(int16_t);
constexpr int CH2 = 2;
constexpr int CH6 = 6;

struct Amplifier { // controls the amplifier's standby state
    static constexpr long MUTE_BURSTS   = 60; // ~2s of digital silence before standby (amp stays cool)
    static constexpr long UNMUTE_BURSTS = 3;  // ~100ms of real audio before leaving standby (unmute late)
    long quiet_bursts = MUTE_BURSTS;          // how many consecutive silent bursts have been observed
    long loud_bursts  = 0;                    // how many consecutive loud bursts have been observed
    bool standby_state = true;                // start muted and say so before any sound can reach the speakers
    FILE* stb_file = fopen("/sys/class/gpio/gpio354/value", "w");
    Amplifier() { write(); }
    ~Amplifier() { stb_set(true); }
    void write() {fputc(standby_state ? '0' : '1', stb_file); fflush(stb_file);}
    void stb_set(bool standby) { if (standby_state != standby) {standby_state = standby; write();} }
    void mute() { stb_set(true); quiet_bursts = MUTE_BURSTS; loud_bursts = 0;}
    void observe(const int16_t* samples, long count) { // scanning for non-zero samples to control amplifier
        while (count-- > 0) if (*samples++) {          // found a non-zero sample, consider it a loud burst
            quiet_bursts = 0;
            if (++loud_bursts >= UNMUTE_BURSTS) stb_set(false);
            return;
        }                                              // all samples were zero, consider it a quiet burst
        loud_bursts = 0;
        if (++quiet_bursts >= MUTE_BURSTS) stb_set(true);
    }
} amplifier;

static bool check_stdin() {
    struct pollfd pfd{ STDIN_FILENO, POLLIN, 0 };
    if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
        char buf[64];
        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
        if (n > 0 && buf[0] == 'q') return false;
        amplifier.write();
    }
    return true;
}

static void info(int error, const char* fmt, ...) {
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
    fputc('\n', stderr);
    if (error < 0 && error > -126) exit(-error);
}

struct AlsaDevice {
    const char* name;              // device name
    unsigned long period_frames;   // number of frames per period
    unsigned long buffer_frames;   // total number of frames in the buffer
    unsigned int rate = 48000;     // sample rate for the device (will be modified by Alsa)
    snd_pcm_t* id = nullptr;       // ALSA PCM handle
    std::vector<int16_t> buf{};    // audio buffer for the device
    void write_all(snd_pcm_sframes_t frames) {
        int16_t* write_at = buf.data(); // write cursor
        while (frames > 0) {
            snd_pcm_sframes_t written = snd_pcm_writei(id, write_at, frames);
            if (written < 0) {
                int recovered = snd_pcm_recover(id, (int)written, 1);
                if (recovered < 0) info(-1, "playback unrecoverable: %s", snd_strerror(recovered));
                continue;
            }
            if (written == 0) info(-1, "playback wrote no frames");
            write_at += written * CH6;
            frames -= written;
        }
    }
    long read_wait(long at) {
        constexpr int CAP_WAIT_MS = 30; // capture wait time in milliseconds, timeout => no light
        int ready = snd_pcm_wait(id, CAP_WAIT_MS);
        if (ready == 0) return -1;
        if (ready < 0 && snd_pcm_recover(id, ready, 1) < 0)
            info(-1, "wait capture unrecoverable: %s", snd_strerror(ready));
        long room = min((unsigned long)period_frames, (buf.size() - at) / CH2); // never read past the end of buf
        long n = snd_pcm_readi(id, &buf[at], room);
        if (n < 0 && snd_pcm_recover(id, (int)n, 1) < 0)
            info(-1, "read capture unrecoverable: %s", snd_strerror(n));
        return n;
    }
    ~AlsaDevice() {if (id) snd_pcm_close(id);}
    AlsaDevice(const char* n, bool capture, unsigned long p, unsigned long b):
            name(n), period_frames(p), buffer_frames(b) {
        // Open and configure PCM device and start capture
        snd_pcm_stream_t stream = capture ? SND_PCM_STREAM_CAPTURE : SND_PCM_STREAM_PLAYBACK;
        int channels            = capture ? CH2 : CH6;
        int mode                = capture ? SND_PCM_NONBLOCK : 0;
        snd_pcm_hw_params_t *hw;
        int err;

        err = snd_pcm_open(&id, name, stream, mode);
        if (err < 0) info(-1, "open %s failed: %s", name, snd_strerror(err));
        snd_pcm_hw_params_alloca(&hw);
        snd_pcm_hw_params_any(id, hw);
        snd_pcm_hw_params_set_access(id, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
        snd_pcm_hw_params_set_format(id, hw, SND_PCM_FORMAT_S16_LE);
        snd_pcm_hw_params_set_channels(id, hw, channels);
        snd_pcm_hw_params_set_rate_near(id, hw, &rate, 0);
        snd_pcm_hw_params_set_period_size_near(id, hw, &period_frames, 0);
        snd_pcm_hw_params_set_buffer_size_near(id, hw, &buffer_frames);
        err = snd_pcm_hw_params(id, hw);
        if (err < 0) info(-1, "set hw_params on %s failed: %s", name, snd_strerror(err));
        err = snd_pcm_prepare(id);
        if (err < 0) info(-1, "prepare %s failed: %s", name, snd_strerror(err));
        info(0, "%s: rate=%u period=%lu buffer=%lu channels=%u", name, rate, period_frames,
                    buffer_frames, channels);
        buf.resize(buffer_frames * channels);
        if (capture) {
            err = snd_pcm_start(id);
            if (err < 0) info(-1, "capture start failed: %s", snd_strerror(err));
        }
    }
};

AlsaDevice cap("hw:CARD=ICUSBAUDIO7D,DEV=0", true,  240/*~5ms*/, AC3_BURST * 2);
AlsaDevice out("Surround",                   false, AC3_BURST,   AC3_BURST * 2);

void upmix(long frames) {
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
}

struct Ac3Decoder {
    AVCodecContext* codec = nullptr;
    SwrContext*       swr = nullptr;
    AVPacket*      packet = nullptr;
    AVFrame*        frame = nullptr;
    ~Ac3Decoder() {
        swr_free(&swr);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codec);
    }
    Ac3Decoder() {
        const AVCodec* ac3 = avcodec_find_decoder(AV_CODEC_ID_AC3);
        if (!ac3) info(-1, "AC-3 decoder not available");
        codec = avcodec_alloc_context3(ac3);
        packet = av_packet_alloc();
        frame = av_frame_alloc();
        if (!codec || !packet || !frame || avcodec_open2(codec, ac3, nullptr) < 0)
            info(-1, "could not open AC-3 decoder");
    }
    int decode(const uint8_t* payload, size_t payload_bytes) {
        if (av_new_packet(packet, (int)payload_bytes) < 0) info(-1, "could not allocate AC-3 packet");
        swab(payload, packet->data, payload_bytes);
        int ret = avcodec_send_packet(codec, packet);
        av_packet_unref(packet);
        if (ret < 0) {
            char error[64]; av_strerror(ret, error, sizeof(error));
            info(-1,"AC-3 packet rejected: %s", error);
        }
        ret = avcodec_receive_frame(codec, frame);
        if (ret < 0) {
            char error[64]; av_strerror(ret, error, sizeof(error));
            info(-1, "AC-3 decode failed: %s", error);
        }
        if (!swr) {
            int64_t input_layout = frame->channel_layout ? frame->channel_layout :
                av_get_default_channel_layout(frame->channels);
            swr = swr_alloc_set_opts(nullptr, AV_CH_LAYOUT_5POINT1, AV_SAMPLE_FMT_S16, frame->sample_rate,
                input_layout, (AVSampleFormat)frame->format, frame->sample_rate, 0, nullptr);
            if (!swr || swr_init(swr) < 0) info(-1, "could not initialize AC-3 resampler");
        }
        uint8_t* output = (uint8_t*)out.buf.data();
        int converted = swr_convert(swr, &output, AC3_BURST,
            (const uint8_t**)frame->extended_data, frame->nb_samples);
        if (converted < 0) info(-1, "could not convert AC-3 frame");
        return converted;
    }
} ac3_decoder;

int main() {
    snd_pcm_reset(cap.id);    // to have clean buffer (reduce delay)
    long have  = 0;           // samples carried from a capture read that crossed a burst boundary
    enum class State {none, pcm, ac3, other} mode = State::none; // none=no light, other=unknown codec 

    while (check_stdin()) {
        constexpr long burst_samples = AC3_BURST * CH2;
        long target   = burst_samples;
        long preamble = -1;             // position of the AC-3 preamble in the capture buffer
        State state   = State::pcm;

        auto scan = [&](long from, long to) {
            for (long i = from; preamble < 0 && i + 4 <= to; i += 2) {
                if ((uint16_t)cap.buf[i] != 0xf872 || (uint16_t)cap.buf[i + 1] != 0x4e1f) continue;
                long words = ((uint16_t)cap.buf[i + 3] + 15) / 16;       // Pd is the payload size in bits
                words += words & 1;                                      // keep cap.buf frame aligned
                if (words < 2 || i + 4 + words > (long)cap.buf.size() || // ensure the payload fits in the buffer
                    i + burst_samples > (long)cap.buf.size()) continue;  // ensure we have enough samples for a full burst
                preamble = i;
                target   = i + burst_samples;
                state    = ((uint16_t)cap.buf[i + 2] & 0x7f) == 1 ? State::ac3 : State::other;
            }
        };

        scan(0, have); // the previous read may already contain the next burst's preamble
        while (have < target) {
            long n = cap.read_wait(have);
            if (n == -1) {amplifier.mute(); mode = State::none; have = 0;} // no light (signal)
            if (n <=  0) continue;                                         // nothing read, try again

            long from = max(0L, have - 2);
            have += n * CH2;
            scan(from, have);
        }
        
        if (state != mode) { amplifier.mute(); mode = state; }
        constexpr int frame_size = AC3_BURST * CH6 * sz16;
        if (state == State::pcm) upmix(AC3_BURST);
        else if (state == State::ac3) {
            int decoded = ac3_decoder.decode((uint8_t*)(cap.buf.data() + preamble + 4),
                                             (uint16_t)cap.buf[preamble + 3] / 8);
            if (decoded != AC3_BURST) {amplifier.mute(); memset(out.buf.data(), 0, frame_size);}
            else for (int i = 0; i < AC3_BURST; i++) { // ffmpeg speaker order -> "Surround" order
                int16_t* s = out.buf.data() + i * CH6;
                swap_ranges(s + 2, s + 4, s + 4); // FC/LFE <-> BL/BR
            }
        } else memset(out.buf.data(), 0, frame_size);  // DTS & friends: never send raw data to speakers

        amplifier.observe(out.buf.data(), AC3_BURST * CH6);
        out.write_all(AC3_BURST);
        have -= target;
        if (have > 0) memmove(cap.buf.data(), &cap.buf[target], have * sz16);
    }
    info(0, "shutting down");
    return 0;
}
