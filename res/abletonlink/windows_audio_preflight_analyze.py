import argparse
import json
import math
import os
from pathlib import Path
import subprocess
import time

import cv2
import numpy as np
import soundfile as sf


def import_soundcard():
    if os.name == "nt":
        # soundcard's Windows backend only needs this to special-case Windows 8.
        # Python's platform.win32_ver() can hang on machines where WMI/CIM
        # queries are unhealthy, so avoid that path for validation tooling.
        import platform

        platform.win32_ver = lambda: ("10", "", "", "")  # type: ignore[assignment]
    import soundcard as sc

    return sc


def dbfs(value: float) -> float:
    if value <= 1e-12:
        return -240.0
    return 20.0 * math.log10(value)


def load_mono(path: Path):
    data, sample_rate = sf.read(str(path), always_2d=True)
    data = data.astype(np.float64)
    mono = data.mean(axis=1)
    return mono, int(sample_rate)


def metrics(samples: np.ndarray) -> dict:
    if samples.size == 0:
        return {
            "rms": 0.0,
            "rms_dbfs": -240.0,
            "peak": 0.0,
            "peak_dbfs": -240.0,
            "clipped_samples": 0,
        }
    abs_samples = np.abs(samples)
    rms = float(np.sqrt(np.mean(np.square(samples))))
    peak = float(np.max(abs_samples))
    return {
        "rms": rms,
        "rms_dbfs": round(dbfs(rms), 2),
        "peak": peak,
        "peak_dbfs": round(dbfs(peak), 2),
        "clipped_samples": int(np.sum(abs_samples >= 0.999)),
    }


def frame_rms(samples: np.ndarray, sample_rate: int, frame_ms: int = 20) -> np.ndarray:
    frame = max(1, int(sample_rate * frame_ms / 1000))
    count = samples.size // frame
    if count <= 0:
        return np.array([], dtype=np.float64)
    trimmed = samples[: count * frame].reshape(count, frame)
    return np.sqrt(np.mean(np.square(trimmed), axis=1))


def detect_transients(samples: np.ndarray, sample_rate: int, baseline_rms: float) -> tuple[int, list[float]]:
    envelope = frame_rms(samples, sample_rate, 10)
    if envelope.size == 0:
        return 0, []
    threshold = max(baseline_rms * 5.0, float(np.percentile(envelope, 90)) * 0.65, 0.002)
    hot = envelope > threshold
    events = []
    min_gap_frames = max(1, int(0.18 / 0.010))
    last = -min_gap_frames
    for idx, active in enumerate(hot):
        if active and idx - last >= min_gap_frames:
            start = max(0, idx - 2)
            stop = min(envelope.size, idx + min_gap_frames)
            peak_idx = start + int(np.argmax(envelope[start:stop]))
            events.append(round(peak_idx * 0.010, 3))
            last = peak_idx
    return len(events), events[:40]


def dropout_windows(samples: np.ndarray, sample_rate: int, baseline_rms: float) -> int:
    envelope = frame_rms(samples, sample_rate, 100)
    if envelope.size == 0:
        return 0
    threshold = max(baseline_rms * 1.5, 0.001)
    return int(np.sum(envelope < threshold))


def draw_waveform_png(
    baseline: np.ndarray,
    playback: np.ndarray,
    sample_rate: int,
    output: Path,
    title: str,
) -> None:
    width = 1400
    height = 720
    image = np.full((height, width, 3), 248, dtype=np.uint8)
    dark = (36, 45, 55)
    blue = (128, 111, 11)
    green = (73, 122, 22)
    red = (32, 38, 166)

    cv2.putText(image, title, (28, 40), cv2.FONT_HERSHEY_SIMPLEX, 0.85, dark, 2, cv2.LINE_AA)
    panels = [
        ("Baseline capture", baseline, 90, 300, blue),
        ("Playback capture", playback, 390, 620, green),
    ]
    for label, samples, top, bottom, color in panels:
        cv2.rectangle(image, (24, top), (width - 24, bottom), (226, 232, 238), 1)
        cv2.putText(image, label, (34, top - 14), cv2.FONT_HERSHEY_SIMPLEX, 0.65, dark, 2, cv2.LINE_AA)
        mid = (top + bottom) // 2
        cv2.line(image, (28, mid), (width - 28, mid), (210, 216, 224), 1)
        if samples.size == 0:
            continue
        samples = samples / max(0.001, float(np.max(np.abs(samples))))
        bucket = max(1, samples.size // (width - 70))
        xs = []
        highs = []
        lows = []
        for x in range((samples.size // bucket)):
            chunk = samples[x * bucket : (x + 1) * bucket]
            xs.append(35 + x)
            highs.append(float(np.max(chunk)))
            lows.append(float(np.min(chunk)))
        scale = (bottom - top) * 0.45
        for x, high, low in zip(xs, highs, lows):
            y1 = int(mid - high * scale)
            y2 = int(mid - low * scale)
            cv2.line(image, (x, y1), (x, y2), color, 1)
        rms = math.sqrt(float(np.mean(np.square(samples))))
        cv2.putText(
            image,
            f"normalized RMS {rms:.4f}",
            (width - 270, top + 26),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.5,
            red,
            1,
            cv2.LINE_AA,
        )
    cv2.imwrite(str(output), image)


def generate_click(output: Path, seconds: int, sample_rate: int = 48000) -> None:
    total = seconds * sample_rate
    data = np.zeros(total, dtype=np.float32)
    click_len = int(sample_rate * 0.025)
    click = np.sin(2 * np.pi * 1800 * np.arange(click_len) / sample_rate).astype(np.float32)
    click *= np.hanning(click_len).astype(np.float32)
    click *= 0.65
    for t in np.arange(0.5, seconds - 0.2, 0.5):
        start = int(t * sample_rate)
        stop = min(total, start + click_len)
        data[start:stop] += click[: stop - start]
    sf.write(str(output), data, sample_rate, subtype="PCM_16")


def hidden_subprocess_kwargs() -> dict:
    if os.name != "nt":
        return {}
    startupinfo = subprocess.STARTUPINFO()
    startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startupinfo.wShowWindow = 0
    return {
        "startupinfo": startupinfo,
        "creationflags": getattr(subprocess, "CREATE_NO_WINDOW", 0),
    }


def record_loopback(args) -> None:
    try:
        sc = import_soundcard()
    except ImportError as exc:
        raise RuntimeError(
            "The Python 'soundcard' package is required for WASAPI loopback. "
            "Install it with: python -m pip install --user soundcard"
        ) from exc

    speaker = None
    speakers = sc.all_speakers()
    if args.speaker_id:
        for candidate in speakers:
            if candidate.id == args.speaker_id or candidate.name == args.speaker_id:
                speaker = candidate
                break
        if speaker is None:
            names = ", ".join(s.name for s in speakers)
            raise RuntimeError(f"Speaker '{args.speaker_id}' not found. Available: {names}")
    else:
        speaker = sc.default_speaker()

    loopback = sc.get_microphone(id=speaker.id, include_loopback=True)
    proc = None
    sample_rate = int(args.sample_rate)
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)

    with loopback.recorder(samplerate=sample_rate, channels=2) as recorder:
        if args.play_file:
            time.sleep(float(args.pre_roll_seconds))
            proc = subprocess.Popen(
                [
                    "ffplay",
                    "-hide_banner",
                    "-nodisp",
                    "-autoexit",
                    "-volume",
                    str(args.volume),
                    args.play_file,
                ],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                **hidden_subprocess_kwargs(),
            )
        data = recorder.record(numframes=int(sample_rate * float(args.seconds)))

    if proc is not None:
        proc.wait(timeout=max(10.0, float(args.seconds) + 5.0))
        if proc.returncode != 0:
            raise RuntimeError(f"ffplay exited with code {proc.returncode}")

    sf.write(str(output), data, sample_rate, subtype="PCM_16")
    if args.metadata_json:
        Path(args.metadata_json).write_text(
            json.dumps(
                {
                    "speaker_name": speaker.name,
                    "speaker_id": speaker.id,
                    "sample_rate": sample_rate,
                    "seconds": float(args.seconds),
                    "channels": 2,
                    "play_file": args.play_file,
                },
                indent=2,
            ),
            encoding="utf-8",
        )


def analyze(args) -> None:
    baseline, baseline_rate = load_mono(Path(args.baseline))
    playback, playback_rate = load_mono(Path(args.playback))
    if baseline_rate != playback_rate:
        raise RuntimeError(f"Sample-rate mismatch: {baseline_rate} vs {playback_rate}")

    baseline_metrics = metrics(baseline)
    playback_metrics = metrics(playback)
    rms_increase = dbfs(playback_metrics["rms"] / max(baseline_metrics["rms"], 1e-12))
    transient_count, transient_times = detect_transients(
        playback, playback_rate, baseline_metrics["rms"]
    )
    dropouts = dropout_windows(playback, playback_rate, baseline_metrics["rms"])
    signal_detected = (
        playback_metrics["rms"] > max(baseline_metrics["rms"] * 2.0, 0.0015)
        or playback_metrics["peak"] > max(baseline_metrics["peak"] * 2.0, 0.015)
    )
    transients_detected = transient_count >= 6

    result = {
        "capture_device": args.capture_device,
        "output_dir": args.output_dir,
        "sample_rate": playback_rate,
        "baseline": baseline_metrics,
        "playback": playback_metrics,
        "rms_increase_db": round(rms_increase, 2),
        "signal_detected": bool(signal_detected),
        "transients_detected": bool(transients_detected),
        "transient_count": transient_count,
        "transient_times_seconds": transient_times,
        "dropout_windows": dropouts,
    }
    Path(args.output_json).write_text(json.dumps(result, indent=2), encoding="utf-8")
    draw_waveform_png(
        baseline,
        playback,
        playback_rate,
        Path(args.output_png),
        f"Audio preflight: {args.capture_device}",
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)

    gen = sub.add_parser("generate-click")
    gen.add_argument("--output", required=True)
    gen.add_argument("--seconds", type=int, default=8)

    loopback = sub.add_parser("record-loopback")
    loopback.add_argument("--output", required=True)
    loopback.add_argument("--seconds", type=float, default=5.0)
    loopback.add_argument("--sample-rate", type=int, default=48000)
    loopback.add_argument("--speaker-id", default="")
    loopback.add_argument("--play-file", default="")
    loopback.add_argument("--volume", type=int, default=35)
    loopback.add_argument("--pre-roll-seconds", type=float, default=0.35)
    loopback.add_argument("--metadata-json", default="")

    ana = sub.add_parser("analyze")
    ana.add_argument("--baseline", required=True)
    ana.add_argument("--playback", required=True)
    ana.add_argument("--output-json", required=True)
    ana.add_argument("--output-png", required=True)
    ana.add_argument("--capture-device", required=True)
    ana.add_argument("--output-dir", required=True)

    args = parser.parse_args()
    if args.command == "generate-click":
        generate_click(Path(args.output), args.seconds)
    elif args.command == "record-loopback":
        record_loopback(args)
    elif args.command == "analyze":
        analyze(args)


if __name__ == "__main__":
    main()
