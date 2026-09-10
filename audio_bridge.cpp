// Compile: g++ -O2 -Wall -std=c++23 -o audio_bridge audio_bridge.cpp -lasound
#include <alsa/asoundlib.h>
#include <poll.h>
#include <unistd.h>
#include <signal.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>

constexpr unsigned int RATE = 48000;
constexpr int CAP_CHANNELS  = 2;
constexpr int OUT_CHANNELS  = 6;
constexpr int PERIOD_FRAMES = 240;   // 5ms @ 48kHz
constexpr int BUFFER_FRAMES = PERIOD_FRAMES * 4;
constexpr int CAP_WAIT_MS   = 500;  // no data within this window -> "off"

// AC3 frame = 1536 samples = 32ms @ 48kHz. Rolling scan window must span at
// least one full frame so a periodic preamble is never missed between reads.
constexpr size_t SCAN_WINDOW = 8192; // bytes, comfortably > one frame

static volatile sig_atomic_t running = 1;
static void on_signal(int) { running = 0; }

struct State {
    enum class Status { off, none, pcm, ac3, dts } current{Status::off};
    using enum Status;
    [[nodiscard]] constexpr const char* str() const {
        switch (current) {
            case off:  return "off";
            case none: return "none";
            case pcm:  return "pcm";
            case ac3:  return "ac3";
            case dts:  return "dts";
        }
        __builtin_unreachable();
    }
    void print() const {
        std::fputs(str(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
    }
    void set(Status s) {
        if (s != current) {
            current = s;
            print();
        }
    }
} state;

#define SND_ERR(format) if(err < 0){fprintf(stderr,format,name,snd_strerror(err));exit(1);}
static snd_pcm_t *open_pcm(const char *name, snd_pcm_stream_t stream, unsigned int channels) {
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

    snd_pcm_uframes_t period = PERIOD_FRAMES;
    snd_pcm_hw_params_set_period_size_near(handle, hw, &period, 0);

    snd_pcm_uframes_t buffer = BUFFER_FRAMES;
    snd_pcm_hw_params_set_buffer_size_near(handle, hw, &buffer);

    err = snd_pcm_hw_params(handle, hw);
    SND_ERR("set hw_params on %s failed: %s\n");

    fprintf(stderr, "%s: rate=%u period=%lu buffer=%lu channels=%u\n",
                    name, rate, period, buffer, channels);

    err = snd_pcm_prepare(handle);
    SND_ERR("prepare %s failed: %s\n");

    return handle;
}

static void poll_stdin() {
    struct pollfd pfd{ STDIN_FILENO, POLLIN, 0 };
    if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
        char buf[64];
        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
        if (n > 0) state.print();
    }
}

// --- stage 2: bitstream detection (detect only, no decode yet) ---

static uint8_t scan_buf[SCAN_WINDOW];
static size_t  scan_len = 0;
constexpr int BITSTREAM_MISS_LIMIT = 10; // periods without a hit before dropping lock (~50ms, > one AC3/DTS burst period)
static int bitstream_miss_count = 0;

static const uint8_t PREAMBLE[4] = {0x72, 0xF8, 0x1F, 0x4E};

// Appends new bytes, trims from the front to stay within SCAN_WINDOW,
// scans for a sync burst. Returns pcm/none as "not found" - caller decides
// which based on silence check.
static State::Status scan_bitstream(const uint8_t* data, size_t len) {
    if (len >= SCAN_WINDOW) {
        std::memcpy(scan_buf, data + (len - SCAN_WINDOW), SCAN_WINDOW);
        scan_len = SCAN_WINDOW;
    } else {
        size_t keep = SCAN_WINDOW - len;
        if (scan_len > keep) scan_len = keep;
        std::memmove(scan_buf, scan_buf + (scan_len > keep ? 0 : 0), 0); // no-op, kept for clarity
        std::memmove(scan_buf, scan_buf + (SCAN_WINDOW - scan_len - len > 0 ? 0 : 0), 0);
        // simpler: shift existing tail left, append new data
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
            if (data_type == 0x01) return State::ac3;
            if (data_type == 0x0B || data_type == 0x0C ||
                data_type == 0x0D || data_type == 0x11) return State::dts;
        }
    }
    return State::none;
}

int main() {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    snd_pcm_t* out = open_pcm("Surround", SND_PCM_STREAM_PLAYBACK, OUT_CHANNELS);
    snd_pcm_t* cap = open_pcm("opt_dsnoop", SND_PCM_STREAM_CAPTURE, CAP_CHANNELS);

    int err = snd_pcm_start(cap);
    if (err < 0) {
        std::fprintf(stderr, "capture start failed: %s\n", snd_strerror(err));
        return 1;
    }
    int16_t cap_buf[PERIOD_FRAMES * CAP_CHANNELS];
    int16_t out_buf[PERIOD_FRAMES * OUT_CHANNELS];

    unsigned long xrun_count = 0;

    while (running) {
        poll_stdin();

        int ready = snd_pcm_wait(cap, CAP_WAIT_MS);
        if (ready == 0) {
            state.set(State::off); // no data within timeout - no lock, no light
            continue;
        }
        if (ready < 0) {
            std::fprintf(stderr, "capture wait error: %s\n", snd_strerror(ready));
            break; // let supervisor restart us
        }

        snd_pcm_sframes_t frames = snd_pcm_readi(cap, cap_buf, PERIOD_FRAMES);
        if (frames < 0) {
            std::fprintf(stderr, "capture read error: %s\n", snd_strerror((int)frames));
            break; // USB likely gone - terminate, let supervisor restart
        }

        bool silent = true;
        for (snd_pcm_sframes_t i = 0; i < frames * CAP_CHANNELS; i++) {
            if (cap_buf[i] != 0) { silent = false; break; }
        }

        auto bitstream = scan_bitstream(reinterpret_cast<uint8_t*>(cap_buf),
                                 frames * CAP_CHANNELS * sizeof(int16_t));

        State::Status detected;
        if (bitstream == State::ac3 || bitstream == State::dts) {
            bitstream_miss_count = 0;
            detected = bitstream;
        } else if (state.current == State::ac3 || state.current == State::dts) {
            // currently locked - grace period before actually dropping
            if (++bitstream_miss_count < BITSTREAM_MISS_LIMIT) {
                detected = state.current;   // hold last known bitstream state
            } else {
                detected = silent ? State::none : State::pcm;
            }
        } else {
            detected = silent ? State::none : State::pcm;
        }
        state.set(detected);
        if (bitstream_miss_count > 0)
            std::fprintf(stderr, "bitstream miss #%d (holding %s)\n", bitstream_miss_count, state.str());

        if (state.current == State::ac3 || state.current == State::dts) {
            // detected but not decoded yet (stage 3) - mute, don't blast
            // compressed bytes into the speakers as if they were PCM.
            std::memset(out_buf, 0, sizeof(out_buf));
        } else {
            for (snd_pcm_sframes_t i = 0; i < frames; i++) {
                int16_t l = cap_buf[i * CAP_CHANNELS + 0];
                int16_t r = cap_buf[i * CAP_CHANNELS + 1];
                out_buf[i * OUT_CHANNELS + 0] = 0; // l;
                out_buf[i * OUT_CHANNELS + 1] = 0; // r;
                out_buf[i * OUT_CHANNELS + 2] = 0; // l / 2 + r / 2; // center
                out_buf[i * OUT_CHANNELS + 3] = 0; // 0;             // LFE
                out_buf[i * OUT_CHANNELS + 4] = 0; // l;
                out_buf[i * OUT_CHANNELS + 5] = 0; // r;
            }
        }

        snd_pcm_sframes_t written = snd_pcm_writei(out, out_buf, frames);
        if (written < 0) {
            xrun_count++;
            written = snd_pcm_recover(out, written, 1);
            if (written < 0) {
                std::fprintf(stderr, "playback unrecoverable: %s\n", snd_strerror((int)written));
                break;
            }
        }
    }

    std::fprintf(stderr, "shutting down, xrun_count=%lu\n", xrun_count);
    snd_pcm_close(cap);
    snd_pcm_close(out);
    return 0;
}