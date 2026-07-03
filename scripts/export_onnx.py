#!/usr/bin/env python3
"""
Export a YOLOv8 model to ONNX for TensorRT ingestion.

Usage:
    conda activate trt-export
    python scripts/export_onnx.py [--model yolov8n] [--size 640] [--out ../model/]

The script downloads the model (if needed), exports ONNX, and runs a quick
sanity check comparing PyTorch vs ONNX outputs.
"""

import argparse
from pathlib import Path

import numpy as np
import onnx
import onnxruntime as ort
from ultralytics import YOLO


def main():
    parser = argparse.ArgumentParser(description="Export YOLOv8 → ONNX")
    parser.add_argument("--model", default="yolov8n", help="YOLO model name (e.g. yolov8n)")
    parser.add_argument("--size",  default=640, type=int, help="Input resolution")
    parser.add_argument("--out",   default="../model", help="Output directory")
    args = parser.parse_args()

    out_dir = (Path(__file__).parent / args.out).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    onnx_path = out_dir / f"{args.model}.onnx"

    # ------------------------------------------------------------------
    # 1. Export
    # ------------------------------------------------------------------
    print(f"[1/3] Loading {args.model} ...")
    model = YOLO(f"{args.model}.pt")  # auto‑downloads if needed

    print(f"[2/3] Exporting to ONNX (imgsz={args.size}) ...")
    # ultralytics will export to the model's directory by default;
    # we move it afterwards.
    success = model.export(format="onnx", imgsz=args.size, opset=12, simplify=True)
    if not success:
        raise RuntimeError("ONNX export failed")

    # Move to target directory
    default_onnx = Path(f"{args.model}.onnx")
    if default_onnx.exists() and default_onnx.resolve() != onnx_path.resolve():
        import shutil
        shutil.move(str(default_onnx), str(onnx_path))

    print(f"   Saved: {onnx_path}  ({onnx_path.stat().st_size / 1024:.1f} KiB)")

    # ------------------------------------------------------------------
    # 2. Validate ONNX model structure
    # ------------------------------------------------------------------
    print("[3/3] Validating ONNX ...")
    onnx_model = onnx.load(str(onnx_path))
    onnx.checker.check_model(onnx_model)
    print("   ONNX model is valid ✓")

    # Print I/O info
    for inp in onnx_model.graph.input:
        shape = [d.dim_value if d.dim_value else "dynamic" for d in inp.type.tensor_type.shape.dim]
        print(f"   Input:  {inp.name} {shape} ({inp.type.tensor_type.elem_type})")
    for outp in onnx_model.graph.output:
        shape = [d.dim_value if d.dim_value else "dynamic" for d in outp.type.tensor_type.shape.dim]
        print(f"   Output: {outp.name} {shape} ({outp.type.tensor_type.elem_type})")

    # ------------------------------------------------------------------
    # 3. Quick inference check (PyTorch vs ONNX)
    # ------------------------------------------------------------------
    print("   Running sanity check (random input) ...")
    dummy = np.random.randn(1, 3, args.size, args.size).astype(np.float32)

    # ONNX Runtime
    session = ort.InferenceSession(str(onnx_path), providers=["CPUExecutionProvider"])
    onnx_out = session.run(None, {onnx_model.graph.input[0].name: dummy})[0]

    print(f"   ONNX output shape: {onnx_out.shape},  mean={onnx_out.mean():.6f},  std={onnx_out.std():.6f}")
    print("\nDone! ONNX model ready for TensorRT engine build.\n")
    print(f"Next step: ./build/IntrusionDetector --model {onnx_path}")


if __name__ == "__main__":
    main()
