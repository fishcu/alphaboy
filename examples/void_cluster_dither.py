"""Void-and-cluster dither: Ulichney's 3-phase blue noise dither matrix."""
"""Reference: Ulichney, 'The void-and-cluster method for dither array generation' (1993)"""

from pathlib import Path
import numpy as np
import matplotlib.colors as mcolors
import matplotlib.pyplot as plt
import matplotlib
matplotlib.use("Agg")

GRID = 16
TOTAL = GRID * GRID
SIGMA = 1.7


# ---------------------------------------------------------------------------
# Gaussian kernel (toroidal)
# ---------------------------------------------------------------------------

def gaussian_kernel(n: int, sigma: float) -> np.ndarray:
    """Toroidal Gaussian kernel with self-weight zeroed out."""
    k = np.zeros((n, n))
    for dr in range(n):
        for dc in range(n):
            dy = min(dr, n - dr)
            dx = min(dc, n - dc)
            k[dr, dc] = np.exp(-(dx * dx + dy * dy) / (2.0 * sigma * sigma))
    k[0, 0] = 0.0
    return k


def _shifted_kernels(kern: np.ndarray) -> np.ndarray:
    """Kernel rolled to every grid position (for incremental LUT updates)."""
    n = kern.shape[0]
    sk = np.empty((n * n, n, n))
    for i in range(n * n):
        sk[i] = np.roll(np.roll(kern, i // n, axis=0), i % n, axis=1)
    return sk


# ---------------------------------------------------------------------------
# 3-phase dither-array construction (incremental LUT)
# ---------------------------------------------------------------------------

def build_dither_array(sigma: float = SIGMA, seed: int = 184) -> np.ndarray:
    """Ulichney's 3-phase void-and-cluster dither array (ranks 0..255)."""
    rng = np.random.default_rng(seed)
    sk = _shifted_kernels(gaussian_kernel(GRID, sigma))

    def _find(lut, pat, select_ones, maximize):
        sentinel = -np.inf if maximize else np.inf
        masked = np.where(pat == int(select_ones), lut, sentinel)
        best = masked.max() if maximize else masked.min()
        cands = np.flatnonzero(masked == best)
        idx = int(rng.choice(cands)) if len(cands) > 1 else int(cands[0])
        return divmod(idx, GRID)

    # --- Initial binary pattern -----------------------------------------------
    lut = np.zeros((GRID, GRID))
    pat = np.zeros((GRID, GRID), dtype=int)
    for idx in rng.choice(TOTAL, size=max(1, TOTAL // 10), replace=False):
        r, c = divmod(int(idx), GRID)
        pat[r, c] = 1
        lut += sk[r * GRID + c]

    for _ in range(TOTAL * 100):
        cr, cc = _find(lut, pat, True, True)
        pat[cr, cc] = 0
        lut -= sk[cr * GRID + cc]
        vr, vc = _find(lut, pat, False, False)
        if (cr, cc) == (vr, vc):
            pat[cr, cc] = 1
            lut += sk[cr * GRID + cc]
            break
        pat[vr, vc] = 1
        lut += sk[vr * GRID + vc]

    ibp = pat.copy()
    ibp_lut = lut.copy()
    n_ones = int(ibp.sum())
    dither = np.empty((GRID, GRID), dtype=int)

    # --- Phase 1: remove from clusters, rank n_ones-1 .. 0 --------------------
    for rank in range(n_ones - 1, -1, -1):
        r, c = _find(lut, pat, True, True)
        dither[r, c] = rank
        pat[r, c] = 0
        lut -= sk[r * GRID + c]

    # --- Phase 2: insert into voids, rank n_ones .. TOTAL//2 - 1 --------------
    pat[:] = ibp
    lut[:] = ibp_lut
    for rank in range(n_ones, TOTAL // 2):
        r, c = _find(lut, pat, False, False)
        dither[r, c] = rank
        pat[r, c] = 1
        lut += sk[r * GRID + c]

    # --- Phase 3: cluster-removal in complement, rank TOTAL//2 .. TOTAL-1 -----
    comp = 1 - pat
    comp_lut = sk[comp.ravel().astype(bool)].sum(axis=0)
    for rank in range(TOTAL // 2, TOTAL):
        r, c = _find(comp_lut, comp, True, True)
        dither[r, c] = rank
        comp[r, c] = 0
        comp_lut -= sk[r * GRID + c]

    return dither


# ---------------------------------------------------------------------------
# Quality metrics
# ---------------------------------------------------------------------------

def radial_anisotropy(dither: np.ndarray) -> float:
    """RMS deviation of DFT magnitude from its radial mean.  Lower = better."""
    n = dither.shape[0]
    mag = np.abs(np.fft.fftshift(np.fft.fft2(
        dither.astype(float) / (n * n - 1))))
    center = n // 2
    mag[center, center] = 0.0

    rows, cols = np.mgrid[:n, :n]
    radii = np.sqrt((rows - center) ** 2.0 + (cols - center) ** 2.0)
    bin_idx = np.round(radii).astype(int)

    ideal = np.zeros_like(mag)
    for b in range(1, bin_idx.max() + 1):
        mask = bin_idx == b
        if mask.any():
            ideal[mask] = mag[mask].mean()

    non_dc = bin_idx > 0
    return float(np.sqrt(np.mean((mag[non_dc] - ideal[non_dc]) ** 2)))


def directional_correlation(dither: np.ndarray) -> float:
    """Max of horizontal/vertical lag-1 Pearson correlation.  Lower = better."""
    vals = dither.astype(float)
    h = float(np.corrcoef(vals.flat, np.roll(vals, -1, axis=1).flat)[0, 1])
    v = float(np.corrcoef(vals.flat, np.roll(vals, -1, axis=0).flat)[0, 1])
    return max(h, v)


# ---------------------------------------------------------------------------
# Visualisation helpers (matching r2_dither.py conventions)
# ---------------------------------------------------------------------------

def invert_dither_array(dither: np.ndarray) -> np.ndarray:
    """Swap roles of position and value: inverted[value] = linear_position."""
    n = dither.shape[0]
    inverted = np.empty_like(dither)
    for row in range(n):
        for col in range(n):
            v = dither[row, col]
            inverted[v // n, v % n] = row * n + col
    return inverted


def render_heatmap(
    grid: np.ndarray, title: str, ax: plt.Axes, fontsize: int = 7,
) -> None:
    cmap = plt.cm.viridis
    norm = mcolors.Normalize(vmin=0, vmax=TOTAL - 1)
    ax.imshow(grid, cmap=cmap, norm=norm, origin="upper")
    for row in range(GRID):
        for col in range(GRID):
            val = grid[row, col]
            rgba = cmap(norm(val))
            luminance = 0.299 * rgba[0] + 0.587 * rgba[1] + 0.114 * rgba[2]
            text_color = "white" if luminance < 0.5 else "black"
            ax.text(
                col, row, f"{val:3d}",
                ha="center", va="center",
                fontsize=fontsize, fontfamily="monospace", color=text_color,
            )
    ax.set_xticks(np.arange(-0.5, GRID, 1), minor=True)
    ax.set_yticks(np.arange(-0.5, GRID, 1), minor=True)
    ax.grid(which="minor", color="gray", linewidth=0.5)
    ax.tick_params(
        which="both", bottom=False, left=False,
        labelbottom=False, labelleft=False,
    )
    ax.set_title(title, fontsize=fontsize + 5)


def render_dft(dither: np.ndarray, title: str, ax: plt.Axes) -> None:
    """Show 2-D DFT magnitude (DC zeroed, log-scaled) to confirm blue-noise."""
    vals = dither.astype(float) / (TOTAL - 1)
    spectrum = np.abs(np.fft.fftshift(np.fft.fft2(vals)))
    spectrum[GRID // 2, GRID // 2] = 0.0
    ax.imshow(
        np.log1p(spectrum), cmap="inferno",
        interpolation="lanczos", origin="upper",
    )
    ax.set_xticks([])
    ax.set_yticks([])
    ax.set_title(title, fontsize=10)


def plot_dither(grid: np.ndarray) -> None:
    fig, ax = plt.subplots(figsize=(10, 10))
    render_heatmap(grid, "Void-and-Cluster Blue Noise Dither (16x16)", ax)
    plt.tight_layout()

    out = Path(__file__).parent / "void_cluster_dither.png"
    fig.savefig(out, dpi=150)
    print(f"Saved {out}")
    plt.close(fig)


def plot_inverted_dither(grid: np.ndarray) -> None:
    inverted = invert_dither_array(grid)
    fig, ax = plt.subplots(figsize=(10, 10))
    render_heatmap(
        inverted,
        "Inverted Void-and-Cluster (fill order -> position index)",
        ax,
    )
    plt.tight_layout()

    out = Path(__file__).parent / "void_cluster_dither_inverted.png"
    fig.savefig(out, dpi=150)
    print(f"Saved {out}")
    plt.close(fig)


# ---------------------------------------------------------------------------
# Seed sweep
# ---------------------------------------------------------------------------

def sweep_seeds(
    n_seeds: int, sigma: float = SIGMA,
) -> tuple[
    list[tuple[int, float, np.ndarray]],
    list[tuple[int, float, np.ndarray]],
    list[tuple[int, float, np.ndarray]],
]:
    """Test seeds 0..n_seeds-1, return best-5 lists for each metric.

    Returns (best_anisotropy, best_correlation, best_combined).
    Combined score = anisotropy + dir_corr  (both lower-is-better).
    """
    rows: list[tuple[int, float, float, np.ndarray]] = []
    for seed in range(n_seeds):
        if seed % 100 == 0:
            print(f"  sweep: {seed}/{n_seeds}")
        d = build_dither_array(sigma, seed)
        rows.append((seed, radial_anisotropy(
            d), directional_correlation(d), d))

    aniso_vals = np.array([t[1] for t in rows])
    corr_vals = np.array([t[2] for t in rows])
    a_mean, a_std = aniso_vals.mean(), aniso_vals.std()
    c_mean, c_std = corr_vals.mean(), corr_vals.std()

    by_aniso = sorted(rows, key=lambda t: t[1])[:5]
    by_corr = sorted(rows, key=lambda t: t[2])[:5]
    by_comb = sorted(
        rows, key=lambda t: (t[1] - a_mean) / a_std + (t[2] - c_mean) / c_std,
    )[:5]

    print(f"  anisotropy  mean={a_mean:.4f}  std={a_std:.4f}")
    print(f"  dir_corr    mean={c_mean:.4f}  std={c_std:.4f}")

    return (
        [(s, a, d) for s, a, _, d in by_aniso],
        [(s, c, d) for s, _, c, d in by_corr],
        [
            (s, (a - a_mean) / a_std + (c - c_mean) / c_std, d)
            for s, a, c, d in by_comb
        ],
    )


def plot_best_seeds(
    results: list[tuple[int, float, np.ndarray]],
    metric_name: str,
    filename: str,
    n_tested: int = 0,
) -> None:
    n = len(results)
    fig, axes = plt.subplots(3, n, figsize=(8 * n, 24))

    for col, (seed, score, dither) in enumerate(results):
        inverted = invert_dither_array(dither)
        render_heatmap(
            dither, f"seed={seed}\n{metric_name}={score:.4f}",
            axes[0, col], fontsize=5,
        )
        render_heatmap(inverted, f"inv  seed={seed}", axes[1, col], fontsize=5)
        render_dft(dither, f"DFT  seed={seed}", axes[2, col])

    fig.suptitle(
        f"Best 5 by {metric_name} (σ={SIGMA}, 16×16, {n_tested} seeds)",
        fontsize=18, y=0.995,
    )
    plt.tight_layout(rect=[0, 0, 1, 0.99])

    out = Path(__file__).parent / filename
    fig.savefig(out, dpi=150)
    print(f"Saved {out}")
    plt.close(fig)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    dither = build_dither_array()
    assert set(dither.flat) == set(range(TOTAL)), \
        "dither array must contain each rank 0..255 exactly once"

    plot_dither(dither)
    plot_inverted_dither(dither)

    n_seeds = 1000
    print(f"Sweeping {n_seeds} seeds …")
    best_aniso, best_corr, best_comb = sweep_seeds(n_seeds)

    print(f"\nBest 5 by radial anisotropy:")
    for seed, score, _ in best_aniso:
        print(f"  seed={seed:4d}  anisotropy={score:.4f}")
    print(f"Best 5 by directional correlation:")
    for seed, score, _ in best_corr:
        print(f"  seed={seed:4d}  dir_corr={score:.4f}")
    print(f"Best 5 by combined (aniso + dir_corr):")
    for seed, score, _ in best_comb:
        print(f"  seed={seed:4d}  combined={score:.4f}")

    plot_best_seeds(best_aniso, "anisotropy",
                    "void_cluster_best_anisotropy.png", n_seeds)
    plot_best_seeds(best_corr, "dir_corr",
                    "void_cluster_best_correlation.png", n_seeds)
    plot_best_seeds(best_comb, "combined",
                    "void_cluster_best_combined.png", n_seeds)
