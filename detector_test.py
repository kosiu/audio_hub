# can detect PCM, AC3, and DTS bitstreams (for DTS 4 sizes of preambles)

import asyncio
import struct
import logging

log = logging.getLogger("audio_mode")

DEV = "opt_dsnoop"          # shared capture device (see asound.conf note below)
DEBOUNCE_COUNT = 3          # consecutive consistent reads before switching
CHUNK_SIZE = 4096
BUF_KEEP = 8192             # bounded rolling buffer size

PREAMBLE = bytes([0x72, 0xF8, 0x1F, 0x4E])  # Pa, Pb little-endian

DTS_PC_VALUES = {0x000B, 0x000C, 0x000D, 0x0015}
AC3_PC_VALUE = 0x0001


def classify(buf: bytes) -> str | None:
    """Return 'ac3', 'dts', or None (no bitstream preamble found)."""
    idx = buf.find(PREAMBLE)
    if idx == -1 or idx + 6 > len(buf):
        return None
    pc = struct.unpack_from("<H", buf, idx + 4)[0]
    if pc == AC3_PC_VALUE:
        return "ac3"
    if pc in DTS_PC_VALUES:
        return "dts"
    return "unknown"


class ModeDetector:
    """Feeds raw PCM chunks, returns a debounced mode: 'ac3' | 'dts' | 'pcm' | None."""

    def __init__(self, debounce: int = DEBOUNCE_COUNT):
        self.buf = bytearray()
        self.debounce = debounce
        self.streak_mode = None
        self.streak_count = 0
        self.confirmed = None

    def feed(self, chunk: bytes) -> str | None:
        self.buf += chunk
        if len(self.buf) > BUF_KEEP:
            del self.buf[:-BUF_KEEP // 2]

        detected = classify(self.buf) or "pcm"

        if detected == self.streak_mode:
            self.streak_count += 1
        else:
            self.streak_mode = detected
            self.streak_count = 1

        if self.streak_count >= self.debounce and self.confirmed != detected:
            self.confirmed = detected
            return detected
        return None


class PipelineManager:
    """Starts/stops the correct ffmpeg decode pipeline based on detected mode."""

    def __init__(self, dev: str):
        self.dev = dev
        self.proc_a = None  # first-stage arecord (bitstream modes)
        self.proc_b = None  # second-stage ffmpeg / passthrough process
        self.current_mode = None

    async def switch_to(self, mode: str):
        if mode == self.current_mode:
            return
        log.info(f"Switching pipeline: {self.current_mode} -> {mode}")
        await self._stop()

        if mode in ("ac3", "dts"):
            self.proc_a = await asyncio.create_subprocess_exec(
                "arecord", "-q", "-D", self.dev, "-f", "S16_LE",
                "-r", "48000", "-c", "2", "-t", "raw",
                "--buffer-time=50000", "--period-time=10000",
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.DEVNULL,
            )
            self.proc_b = await asyncio.create_subprocess_exec(
                "ffmpeg", "-hide_banner", "-loglevel", "warning",
                "-probesize", "32", "-analyzeduration", "0",
                "-fflags", "nobuffer", "-flags", "low_delay",
                "-f", "spdif", "-i", "-",
                "-af",
                "aresample=async=1:out_channel_layout=5.1,"
                "pan=5.1|c0=c0|c1=c1|c2=c4|c3=c5|c4=c2|c5=c3",
                "-c:a", "pcm_s16le", "-f", "alsa", "Surround",
                stdin=self.proc_a.stdout,
                stderr=asyncio.subprocess.DEVNULL,
            )
            self.proc_a.stdout.close()

        else:  # plain stereo PCM passthrough
            self.proc_a = None
            self.proc_b = await asyncio.create_subprocess_exec(
                "ffmpeg", "-hide_banner", "-loglevel", "warning",
                "-fflags", "nobuffer", "-flags", "low_delay",
                "-f", "alsa", "-i", self.dev,
                "-c:a", "pcm_s16le", "-f", "alsa", "Stereo",
                stderr=asyncio.subprocess.DEVNULL,
            )

        self.current_mode = mode

    async def _stop(self):
        for p in (self.proc_b, self.proc_a):
            if p and p.returncode is None:
                p.terminate()
                try:
                    await asyncio.wait_for(p.wait(), timeout=1.0)
                except asyncio.TimeoutError:
                    p.kill()
                    await p.wait()
        self.proc_a = self.proc_b = None


async def detector_loop(dev: str, manager: PipelineManager):
    """Persistent capture + scan loop. Runs forever as an asyncio task."""
    while True:
        proc = None
        try:
            proc = await asyncio.create_subprocess_exec(
                "arecord", "-q", "-D", dev, "-f", "S16_LE",
                "-r", "48000", "-c", "2", "-t", "raw",
                "--buffer-time=50000", "--period-time=10000",
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.DEVNULL,
            )
            detector = ModeDetector()
            while True:
                chunk = await proc.stdout.read(CHUNK_SIZE)
                if not chunk:
                    break  # device dropped / arecord died — restart
                mode = detector.feed(chunk)
                print(f"Detected mode: {mode}")
                if mode:
                    await manager.switch_to(mode)
        except Exception:
            log.exception("detector_loop error, restarting capture")
        finally:
            if proc and proc.returncode is None:
                proc.kill()
                await proc.wait()
        await asyncio.sleep(0.5)  # brief backoff before retrying capture


async def main():
    logging.basicConfig(level=logging.INFO)
    manager = PipelineManager(DEV)
    await detector_loop(DEV, manager)


if __name__ == "__main__":
    asyncio.run(main())
