import numpy as np
from sklearn.neural_network import MLPRegressor
from sklearn.model_selection import train_test_split




NORMAL_DATA_FILE = "normal_data.csv"
WEIGHTS_OUTPUT   = "weights_output.txt"
MSE_PERCENTILE   = 99

HIDDEN_NODES = 3
MAX_ITER     = 2000
RANDOM_SEED  = 42


def format_c_array_1d(name, arr, dtype="float"):
    vals = ", ".join(f"{v:.6f}f" for v in arr)
    return f"static const {dtype} {name}[] = {{ {vals} }};"


def format_c_array_2d(name, arr, rows, cols, dtype="float"):
    lines = [f"static const {dtype} {name}[{rows}][{cols}] = {{"]
    for i, row in enumerate(arr):
        comma = "," if i < len(arr) - 1 else ""
        vals = ", ".join(f"{v:.6f}f" for v in row)
        lines.append(f"    {{ {vals} }}{comma}")
    lines.append("};")
    return "\n".join(lines)


def main():
    print("  Step 2: Train Autoencoder → Generate C Weights")

    try:
        data = np.loadtxt(NORMAL_DATA_FILE, delimiter=",", skiprows=1)
    except FileNotFoundError:
        print(f"\n[ERROR] '{NORMAL_DATA_FILE}' not found!")
        print("  → Run step1_parse_logs.py first.")
        return

    if data.ndim == 1:
        data = data.reshape(1, -1)

    print(f"[LOAD]  {len(data)} normal samples loaded ({data.shape[1]} features each).")

    if len(data) < 20:
        print("[WARNING] Very few samples. Collect at least 60 seconds of normal data.")

    feat_mean = data.mean(axis=0)
    feat_std  = data.std(axis=0)
    feat_std[feat_std == 0] = 1.0

    data_norm = (data - feat_mean) / feat_std

    X_train, X_val = train_test_split(data_norm, test_size=0.2,
                                      random_state=RANDOM_SEED)
    print(f"[SPLIT] Train={len(X_train)} / Val={len(X_val)}")

    print(f"[TRAIN] Training autoencoder (4 → {HIDDEN_NODES} → 4), "
          f"max_iter={MAX_ITER}...")

    model = MLPRegressor(
        hidden_layer_sizes=(HIDDEN_NODES,),
        activation="logistic",
        solver="adam",
        max_iter=MAX_ITER,
        random_state=RANDOM_SEED,
        verbose=False
    )
    model.fit(X_train, X_train)

    recon_train = model.predict(X_train)
    recon_val   = model.predict(X_val)

    mse_train = ((X_train - recon_train) ** 2).mean(axis=1)
    mse_val   = ((X_val   - recon_val)   ** 2).mean(axis=1)

    mse_max = float(np.percentile(mse_val, MSE_PERCENTILE))

    print(f"\n[EVAL]  Train MSE — mean={mse_train.mean():.6f}  "
          f"max={mse_train.max():.6f}")
    print(f"[EVAL]  Val   MSE — mean={mse_val.mean():.6f}  "
          f"99pct={mse_max:.6f}  (→ MSE_MAX)")

    W_enc = model.coefs_[0].T
    b_enc = model.intercepts_[0]
    W_dec = model.coefs_[1].T
    b_dec = model.intercepts_[1]

    mean_vals = "    " + ",\n    ".join(
        f"{feat_mean[i]:.6f}f"
        for i in range(len(feat_mean))
    )
    std_vals = "    " + ",\n    ".join(
        f"{feat_std[i]:.6f}f"
        for i in range(len(feat_std))
    )

    w_enc_str = format_c_array_2d("W_enc", W_enc, HIDDEN_NODES, 4)
    b_enc_str = format_c_array_1d("b_enc", b_enc)
    w_dec_str = format_c_array_2d("W_dec", W_dec, 4, HIDDEN_NODES)
    b_dec_str = format_c_array_1d("b_dec", b_dec)

    output = f"""
static const float FEAT_MEAN[4] = {{
{mean_vals}
}};

static const float FEAT_STD[4] = {{
{std_vals}
}};

{w_enc_str}

{b_enc_str}

{w_dec_str}

{b_dec_str}

static const float MSE_MAX = {mse_max:.6f}f;
"""

    print("\n  COPY THE ARRAYS BELOW INTO anomaly_detector.cpp")
    print(output)

    with open(WEIGHTS_OUTPUT, "w", encoding="utf-8") as f:
        f.write(output)
    print(f"[SAVE]  Weights also saved to '{WEIGHTS_OUTPUT}' for easy copy-paste.")
    print("\n[DONE] Paste the arrays into anomaly_detector.cpp, rebuild, and reflash.")


if __name__ == "__main__":
    main()
