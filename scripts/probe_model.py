#!/usr/bin/env python3
"""Print a .tflite model's input and output tensors, then run it once on a test frame.

Laptop only (dev tool; nothing here runs on the hat). Use it before writing C++ against a model so
the tensor order, shapes and types in device/src/detect come from the model, not from guesses.

    python3 -m venv .dev/venv && .dev/venv/bin/pip install "ai-edge-litert==1.2.0" numpy
    .dev/venv/bin/python scripts/probe_model.py models/efficientdet_lite0.tflite [--threads 4]

Works with any of: tflite_runtime, ai_edge_litert, tensorflow (whichever is installed).
The test frame is synthetic (mid grey); no photo is read or written.
"""
import argparse
import sys
import time


def load_interpreter(path, threads):
    for mod in ("tflite_runtime.interpreter", "ai_edge_litert.interpreter"):
        try:
            m = __import__(mod, fromlist=["Interpreter"])
            return m.Interpreter(model_path=path, num_threads=threads), mod
        except ImportError:
            pass
    try:
        import tensorflow as tf
        return tf.lite.Interpreter(model_path=path, num_threads=threads), "tensorflow"
    except ImportError:
        sys.exit("no TFLite interpreter found: pip install ai-edge-litert (or tflite-runtime)")


def describe(t):
    q = t.get("quantization_parameters", {})
    scales = q.get("scales", [])
    zps = q.get("zero_points", [])
    quant = f" quant(scale={scales[0]:.6g}, zero={zps[0]})" if len(scales) else ""
    return f"[{t['index']}] {t['name']!r} shape={list(t['shape'])} dtype={t['dtype'].__name__}{quant}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model")
    ap.add_argument("--threads", type=int, default=4)
    ap.add_argument("--runs", type=int, default=20)
    a = ap.parse_args()

    import numpy as np

    interp, backend = load_interpreter(a.model, a.threads)
    interp.allocate_tensors()
    print(f"backend: {backend}")
    ins, outs = interp.get_input_details(), interp.get_output_details()
    print("inputs:")
    for t in ins:
        print("  " + describe(t))
    print("outputs (C API order = this order):")
    for i, t in enumerate(outs):
        print(f"  out{i} " + describe(t))

    inp = ins[0]
    frame = np.full(inp["shape"], 128, dtype=inp["dtype"])
    interp.set_tensor(inp["index"], frame)
    interp.invoke()  # warm-up
    t0 = time.perf_counter()
    for _ in range(a.runs):
        interp.invoke()
    ms = (time.perf_counter() - t0) * 1000 / a.runs
    print(f"invoke: {ms:.1f} ms avg over {a.runs} runs ({a.threads} threads, this machine — not the Pi)")
    print("output sample on a grey frame:")
    for i, t in enumerate(outs):
        v = interp.get_tensor(t["index"])
        flat = v.reshape(-1)
        print(f"  out{i} {t['name']!r}: min={flat.min():.3g} max={flat.max():.3g} first={flat[:8].tolist()}")


if __name__ == "__main__":
    main()
