import asyncio
import os
import struct
import logging

log = logging.getLogger("audio_mode")

DEV = "opt_dsnoop"
DEBOUNCE_COUNT = 3
CHUNK_SIZE = 4096
BUF_KEEP = 8192

PREAMBLE = bytes([0x72, 0xF8, 0x1F, 0x4E])  # Pa, Pb little-endian

DTS_PC_VALUES = {0x0B, 0x0C, 0x0D, 0x11}
AC3_PC_VALUE = 0x01


def classify(buf: bytes) -> str | None:
    idx = buf.find(PREAMBLE)
    if idx == -1 or idx + 6 > len(buf):
        return None
    pc = struct.unpack_from("<H", buf, idx + 4)[0]
    data_type = pc & 0x7F
    if data_type == AC3_PC_VALUE:
        return "ac3"
    if data_type in DTS_PC_VALUES:
        return "dts"
    log.warning(f"Preamble found but unrecognized Pc=0x{pc:04X} (data_type=0x{data_type:02X})")
    return None


class ModeDetector:
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
    def __init__(self, dev: str):
        self.dev = dev
        self.proc_a = None
        self.proc_b = None
        self.current_mode = None

    async def switch_to(self, mode: str):
        if mode == self.current_mode:
            return
        log.info(f"Switching pipeline: {self.current_mode} -> {mode}")
        await self._stop()

        if mode in ("ac3", "dts"):
            read_fd, write_fd = os.pipe()

            self.proc_a = await asyncio.create_subprocess_exec(
                "arecord", "-q", "-D", self.dev, "-f", "S16_LE",
                "-r", "48000", "-c", "2", "-t", "raw",
                "--buffer-time=10000", "--period-time=5000",
                stdout=write_fd,
                stderr=asyncio.subprocess.DEVNULL,
            )
            os.close(write_fd)  # parent's copy no longer needed; child has its own

            self.proc_b = await asyncio.create_subprocess_exec(
                "ffmpeg", "-hide_banner", "-loglevel", "warning",
                "-probesize", "32", "-analyzeduration", "0",
                "-fflags", "nobuffer", "-flags", "low_delay",
                "-f", "spdif", "-i", "-",
                "-af",
                "aresample=async=1:out_channel_layout=5.1,"
                "pan=5.1|c0=c0|c1=c1|c2=c4|c3=c5|c4=c2|c5=c3",
                "-c:a", "pcm_s16le", "-f", "alsa", "Surround",
                stdin=read_fd,
                stderr=asyncio.subprocess.DEVNULL,
            )
            os.close(read_fd)  # same here

        else:
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
                    break
                mode = detector.feed(chunk)
                if mode:
                    print(f"Detected mode: {mode}")
                    await manager.switch_to(mode)
        except Exception:
            log.exception("detector_loop error, restarting capture")
        finally:
            if proc and proc.returncode is None:
                proc.kill()
                await proc.wait()
        await asyncio.sleep(0.5)


async def main():
    logging.basicConfig(level=logging.INFO)
    manager = PipelineManager(DEV)
    await detector_loop(DEV, manager)


if __name__ == "__main__":
    asyncio.run(main())
