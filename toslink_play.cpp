// g++ -O2 -Wall -std=c++23 -o toslink_play toslink_play.cpp -lasound -lavformat -lavcodec -lavutil -lswresample
extern "C" {
#include <alsa/asoundlib.h>
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

constexpr int AC3_FRAME_SAMPLES = 1536;  // number of samples per AC3 frame
constexpr int CAP_WAIT_MS       = 30;    // capture wait time in milliseconds

constexpr int CAP_CHANNELS = 2;
constexpr int OUT_CHANNELS = 6;
constexpr int CH_REMAP[OUT_CHANNELS] = {0, 1, 4, 5, 2, 3};

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

static bool check_stdin() {
    struct pollfd pfd{ STDIN_FILENO, POLLIN, 0 };
    if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
        char buf[64];
        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
        if (n > 0) state.print();
        if (buf[0] == 'q') return false;
    }
    return true;
}

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

int main() {
    snd_pcm_reset(cap.id); // to have clean buffer (reduce delay)

    
    while (check_stdin()) {
        long accumulated_index = 0; // index into the captured buffer (in samples, not frames)
        long preamble_index = -1;   // index of preamble of IEC 61937
        bool silent = true;         // whether the captured audio is filled with zeros
        int ready = 0;              // result of snd_pcm_wait()
        while(accumulated_index < (AC3_FRAME_SAMPLES + cap.period_frames) * CAP_CHANNELS) {
            long new_frames = 0;
            ready = snd_pcm_wait(cap.id, CAP_WAIT_MS);
            if (ready < 0)  { log_line("capture wait error: %s\n", snd_strerror(ready)); exit(1); } // xrun is recoverable
            if (ready > 0) new_frames = 
                            snd_pcm_readi(cap.id, &cap.buf[accumulated_index], cap.period_frames);
            accumulated_index += new_frames * CAP_CHANNELS;

            for (long i = 0; i + 2 <= new_frames * CAP_CHANNELS; i += 2) {
                if (cap.buf[i] != 0 || cap.buf[i+1] != 0) silent = false;
                if (cap.buf[i] == (int16_t)0xf872 && (int16_t)cap.buf[i+1] == 0x4e1f) {
                    preamble_index = i;
                    break;
                }
            }
        }
        if      (preamble_index != -1) state.set(State::ac3);
        else if (silent)               state.set(State::none);
        else if (ready == 0)           state.set(State::off);
        else                           state.set(State::pcm);

        if (state.current == State::pcm) {
            for (long i = 0; i < accumulated_index / CAP_CHANNELS; i++) {
                int16_t l = cap.buf[i * CAP_CHANNELS + 0];
                int16_t r = cap.buf[i * CAP_CHANNELS + 1];
                out.buf[i * OUT_CHANNELS + 0] = l;
                out.buf[i * OUT_CHANNELS + 1] = r;
                out.buf[i * OUT_CHANNELS + 2] = l;
                out.buf[i * OUT_CHANNELS + 3] = r;
                out.buf[i * OUT_CHANNELS + 4] = l / 2 + r / 2;
                out.buf[i * OUT_CHANNELS + 5] = 0;
            }
        } else {
            std::fill(out.buf.begin(), out.buf.end(), 0);
        }
        out.write_all(accumulated_index);
    }

    log_line("shutting down");
    return 0;
}
