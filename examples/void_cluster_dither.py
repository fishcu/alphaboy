"""Void-and-cluster dither: Ulichney's 3-phase blue noise dither matrix."""
"""Reference: Ulichney, 'The void-and-cluster method for dither array generation' (1993)"""

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.colors as mcolors
import numpy as np
from pathlib import Path

GRID = 16
TOTAL = GRID * GRID
SIGMA = 1.7


# ---------------------------------------------------------------------------
# Gaussian kernel & energy (toroidal, FFT-based)
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


def energy(pattern: np.ndarray, kern_fft: np.ndarray) -> np.ndarray:
    """Circular convolution of binary pattern with Gaussian kernel."""
    return np.real(np.fft.ifft2(np.fft.fft2(pattern.astype(float)) * kern_fft))


def _pick_extremum(
    e: np.ndarray,
    mask: np.ndarray,
    maximize: bool,
    rng: np.random.Generator,
) -> tuple[int, int, int]:
    """Pick extremal pixel among *mask*-selected positions.

    Returns (row, col, n_tied).  When n_tied > 1 the winner is chosen
    uniformly at random among the tied candidates.
    """
    sentinel = -np.inf if maximize else np.inf
    masked = np.where(mask, e, sentinel)
    best = np.max(masked) if maximize else np.min(masked)
    candidates = np.flatnonzero(masked == best)
    idx = int(rng.choice(candidates)) if len(candidates) > 1 else int(candidates[0])
    return (*divmod(idx, e.shape[1]), len(candidates))


def find_cluster(
    pattern: np.ndarray, kern_fft: np.ndarray, rng: np.random.Generator,
) -> tuple[int, int, int]:
    """1-pixel with max energy (tightest cluster).  Returns (row, col, n_tied)."""
    return _pick_extremum(energy(pattern, kern_fft), pattern == 1, True, rng)


def find_void(
    pattern: np.ndarray, kern_fft: np.ndarray, rng: np.random.Generator,
) -> tuple[int, int, int]:
    """0-pixel with min energy (largest void).  Returns (row, col, n_tied)."""
    return _pick_extremum(energy(pattern, kern_fft), pattern == 0, False, rng)


# ---------------------------------------------------------------------------
# Initial binary pattern
# ---------------------------------------------------------------------------

def initial_binary_pattern(
    n: int, kern_fft: np.ndarray, rng: np.random.Generator,
) -> tuple[np.ndarray, int]:
    """Seed ~10 % ones, then swap tightest-cluster / largest-void until stable.

    Returns (pattern, tie_count).
    """
    n_ones = max(1, n * n // 10)
    pat = np.zeros((n, n), dtype=int)
    for idx in rng.choice(n * n, size=n_ones, replace=False):
        pat[idx // n, idx % n] = 1

    ties = 0
    for _ in range(TOTAL * 100):
        cr, cc, n1 = find_cluster(pat, kern_fft, rng)
        pat[cr, cc] = 0
        vr, vc, n2 = find_void(pat, kern_fft, rng)
        ties += (n1 > 1) + (n2 > 1)
        if (cr, cc) == (vr, vc):
            pat[cr, cc] = 1
            break
        pat[vr, vc] = 1

    return pat, ties


# ---------------------------------------------------------------------------
# 3-phase dither-array construction
# ---------------------------------------------------------------------------

def build_dither_array(sigma: float = SIGMA, seed: int = 42) -> np.ndarray:
    """Ulichney's 3-phase void-and-cluster dither array (ranks 0..255)."""
    rng = np.random.default_rng(seed)
    kern = gaussian_kernel(GRID, sigma)
    kern_fft = np.fft.fft2(kern)

    ibp, ties = initial_binary_pattern(GRID, kern_fft, rng)
    n_ones = int(ibp.sum())

    dither = np.empty((GRID, GRID), dtype=int)

    # Phase 1: remove from clusters, rank n_ones-1 .. 0
    pat = ibp.copy()
    for rank in range(n_ones - 1, -1, -1):
        r, c, n = find_cluster(pat, kern_fft, rng)
        ties += n > 1
        dither[r, c] = rank
        pat[r, c] = 0

    # Phase 2: insert into voids, rank n_ones .. TOTAL//2 - 1
    pat = ibp.copy()
    for rank in range(n_ones, TOTAL // 2):
        r, c, n = find_void(pat, kern_fft, rng)
        ties += n > 1
        dither[r, c] = rank
        pat[r, c] = 1

    # Phase 3: cluster-removal in complement, rank TOTAL//2 .. TOTAL-1
    comp = 1 - pat
    for rank in range(TOTAL // 2, TOTAL):
        r, c, n = find_cluster(comp, kern_fft, rng)
        ties += n > 1
        dither[r, c] = rank
        comp[r, c] = 0

    print(f"Tie-breaks: {ties} (across ~{TOTAL * 2} extremum queries)")
    return dither


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


SIGMAS = [1.50, 1.60, 1.70, 1.80, 1.90]
SEEDS = [7, 13, 99, 137, 200]


def plot_sigma_comparison(sigmas: list[float]) -> None:
    n = len(sigmas)
    fig, axes = plt.subplots(3, n, figsize=(8 * n, 24))

    for col, sigma in enumerate(sigmas):
        print(f"  sigma={sigma} ...")
        dither = build_dither_array(sigma)
        inverted = invert_dither_array(dither)

        render_heatmap(dither, f"Dither  σ={sigma}", axes[0, col], fontsize=5)
        render_heatmap(inverted, f"Inverted  σ={sigma}", axes[1, col], fontsize=5)
        render_dft(dither, f"DFT magnitude  σ={sigma}", axes[2, col])

    fig.suptitle(
        "Void-and-Cluster: sigma comparison (16×16)",
        fontsize=18, y=0.995,
    )
    plt.tight_layout(rect=[0, 0, 1, 0.99])

    out = Path(__file__).parent / "void_cluster_sigma_comparison.png"
    fig.savefig(out, dpi=150)
    print(f"Saved {out}")
    plt.close(fig)


def plot_seed_comparison(seeds: list[int], sigma: float = SIGMA) -> None:
    n = len(seeds)
    fig, axes = plt.subplots(3, n, figsize=(8 * n, 24))

    for col, seed in enumerate(seeds):
        print(f"  seed={seed} ...")
        dither = build_dither_array(sigma, seed)
        inverted = invert_dither_array(dither)

        render_heatmap(dither, f"seed={seed}", axes[0, col], fontsize=5)
        render_heatmap(inverted, f"inv  seed={seed}", axes[1, col], fontsize=5)
        render_dft(dither, f"DFT  seed={seed}", axes[2, col])

    fig.suptitle(
        f"Void-and-Cluster: seed comparison (σ={sigma}, 16×16)",
        fontsize=18, y=0.995,
    )
    plt.tight_layout(rect=[0, 0, 1, 0.99])

    out = Path(__file__).parent / "void_cluster_seed_comparison.png"
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

    print("Generating sigma comparison …")
    plot_sigma_comparison(SIGMAS)

    print("Generating seed comparison …")
    plot_seed_comparison(SEEDS)
