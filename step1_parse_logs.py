import re
import numpy as np

INPUT_FILE   = "raw_serial_log.txt"
NORMAL_OUT   = "normal_data.csv"
ALL_OUT      = "all_data.csv"

NORMAL_SAMPLE_COUNT = 300

SIMULATED_TEMPERATURE = 30.0

def parse_log(filepath):
    pattern = re.compile(
        r"CPU=(\d+)%\s+stackB=(\d+)\s+jitter=(-?\d+)c\s+temp=([0-9.]+)C"
    )

    rows = []
    skipped = 0

    with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            m = pattern.search(line)
            if m:
                cpu_load   = float(m.group(1))
                stack_free = float(m.group(2))
                isr_latency= float(m.group(3))
                temperature= float(m.group(4))
                rows.append([cpu_load, stack_free, isr_latency, temperature])
            else:
                skipped += 1

    print(f"[PARSE] Parsed {len(rows)} samples, skipped {skipped} non-matching lines.")
    return rows


def main():
    print("  Step 1: Parse Serial Logs → Clean CSV")

    try:
        rows = parse_log(INPUT_FILE)
    except FileNotFoundError:
        print(f"\n[ERROR] '{INPUT_FILE}' not found!")
        print("  → Copy your serial terminal output into that file and re-run.")
        return

    if len(rows) == 0:
        print("[ERROR] No valid data lines found. Check your log format.")
        return

    data = np.array(rows, dtype=np.float32)

    np.savetxt(ALL_OUT, data, delimiter=",",
               header="cpu_load,stack_free,isr_latency,temperature",
               comments="")
    print(f"[SAVE]  All data    → {ALL_OUT} ({len(data)} rows)")

    normal = data[:NORMAL_SAMPLE_COUNT]
    if len(normal) == 0:
        print("[WARNING] Not enough samples for normal baseline. Collect more data.")
        return

    np.savetxt(NORMAL_OUT, normal, delimiter=",",
               header="cpu_load,stack_free,isr_latency,temperature",
               comments="")
    print(f"[SAVE]  Normal data → {NORMAL_OUT} ({len(normal)} rows)")

    print("\n[DONE] Run step2_train_model.py next.")
    print(f"\nSample stats (normal data):")
    labels = ["cpu_load", "stack_free", "isr_latency", "temperature"]
    for i, label in enumerate(labels):
        print(f"  {label:15s}  mean={normal[:, i].mean():.2f}  std={normal[:, i].std():.2f}"
              f"  min={normal[:, i].min():.2f}  max={normal[:, i].max():.2f}")


if __name__ == "__main__":
    main()
