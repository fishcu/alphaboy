"""R2 quasirandom dither: pattern generation + animated dissolve on a sprite."""

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.colors as mcolors
import numpy as np
import pygame
from pathlib import Path

GRID = 16
TOTAL = GRID * GRID
SCALE = 8

PHI2 = 1.32471795724474602596
ALPHA1 = 1.0 / PHI2
ALPHA2 = 1.0 / (PHI2 * PHI2)
SEED = 0.5

SKY_BLUE = (135, 206, 235)

# Super Mario Bros Super Mushroom, 16x16, sampled from mushroom.pgm.
# 0=transparent  1=black(outline)  2=dark red  3=bright red  4=off-white  5=tan
PALETTE = np.array([
    [  0,   0,   0,   0],
    [  0,   0,   0, 255],
    [211,  26,  26, 255],
    [255,  26,  26, 255],
    [248, 248, 248, 255],
    [255, 208, 127, 255],
], dtype=np.uint8)

# fmt: off
SPRITE_INDICES = np.array([
    [0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0],
    [0,0,0,1,1,4,4,2,2,2,2,1,1,0,0,0],
    [0,0,1,4,4,4,4,3,3,3,3,4,4,1,0,0],
    [0,1,4,4,4,4,3,3,3,3,3,3,4,4,1,0],
    [0,1,4,4,4,3,3,4,4,4,4,3,3,4,1,0],
    [1,2,3,3,3,3,4,4,4,4,4,4,3,3,2,1],
    [1,2,4,4,3,3,4,4,4,4,4,4,3,3,2,1],
    [1,4,4,4,4,3,4,4,4,4,4,4,3,3,4,1],
    [1,4,4,4,4,3,3,4,4,4,4,3,3,4,4,1],
    [1,2,4,4,2,2,2,2,2,2,2,2,2,4,4,1],
    [1,2,2,2,1,1,1,1,1,1,1,1,2,2,4,1],
    [0,1,1,1,5,5,1,5,5,1,5,5,1,1,1,0],
    [0,0,1,5,5,5,1,5,5,1,5,5,5,1,0,0],
    [0,0,1,5,5,5,5,5,5,5,5,5,5,1,0,0],
    [0,0,0,1,5,5,5,5,5,5,5,5,1,0,0,0],
    [0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0],
])
# fmt: on


def build_dither_array() -> np.ndarray:
    grid = np.full((GRID, GRID), -1, dtype=int)
    filled = 0
    n = 0
    while filled < TOTAL:
        n += 1
        x = (SEED + ALPHA1 * n) % 1.0
        y = (SEED + ALPHA2 * n) % 1.0
        col = int(x * GRID) % GRID
        row = int(y * GRID) % GRID
        if grid[row, col] != -1:
            continue
        grid[row, col] = filled
        filled += 1
    return grid


def build_sprite() -> np.ndarray:
    return PALETTE[SPRITE_INDICES]


def apply_dithered_transparency(
    sprite: np.ndarray, dither: np.ndarray, transparency: float,
) -> np.ndarray:
    result = sprite.copy()
    threshold = int(TOTAL * transparency)
    result[dither < threshold, 3] = 0
    return result


def plot_dither(grid: np.ndarray) -> None:
    fig, ax = plt.subplots(figsize=(10, 10))
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
                fontsize=7, fontfamily="monospace", color=text_color,
            )

    ax.set_xticks(np.arange(-0.5, GRID, 1), minor=True)
    ax.set_yticks(np.arange(-0.5, GRID, 1), minor=True)
    ax.grid(which="minor", color="gray", linewidth=0.5)
    ax.tick_params(
        which="both", bottom=False, left=False,
        labelbottom=False, labelleft=False,
    )
    ax.set_title("R2 Quasirandom Dither Pattern (16\u00d716)", fontsize=14)
    plt.tight_layout()

    out = Path(__file__).parent / "r2_dither.png"
    fig.savefig(out, dpi=150)
    print(f"Saved {out}")
    plt.close(fig)


def blit_sprite(screen: pygame.Surface, sprite: np.ndarray, x: int, y: int) -> None:
    for row in range(GRID):
        for col in range(GRID):
            r, g, b, a = sprite[row, col]
            if a > 0:
                color = (int(r), int(g), int(b))
            else:
                color = SKY_BLUE
            pygame.draw.rect(
                screen, color,
                (x + col * SCALE, y + row * SCALE, SCALE, SCALE),
            )


def show_debug_sprites(sprite: np.ndarray, dithered: np.ndarray) -> None:
    gap = SCALE
    w = GRID * SCALE * 2 + gap * 3
    h = GRID * SCALE + gap * 2 + 30
    screen = pygame.display.set_mode((w, h))
    pygame.display.set_caption("Debug: Original vs Dithered")

    font = pygame.font.SysFont("consolas", 16)

    screen.fill(SKY_BLUE)
    blit_sprite(screen, sprite, gap, gap + 24)
    blit_sprite(screen, dithered, gap * 2 + GRID * SCALE, gap + 24)

    lbl1 = font.render("Original", True, (0, 0, 0))
    lbl2 = font.render("75% opaque (dithered)", True, (0, 0, 0))
    screen.blit(lbl1, (gap, 6))
    screen.blit(lbl2, (gap * 2 + GRID * SCALE, 6))

    pygame.display.flip()

    waiting = True
    while waiting:
        for event in pygame.event.get():
            if event.type in (pygame.QUIT, pygame.KEYDOWN):
                waiting = False


def animate(
    sprite: np.ndarray, dither: np.ndarray, transparency: float,
) -> None:
    dither_1d = dither.flatten()
    current = apply_dithered_transparency(sprite, dither, transparency)
    action_threshold = int(TOTAL * transparency)

    size = GRID * SCALE
    screen = pygame.display.set_mode((size, size))
    pygame.display.set_caption("R2 Dither Dissolve")
    clock = pygame.time.Clock()

    frame = 0
    drift = (dither * 167) % TOTAL

    running = True
    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE:
                running = False

        for row in range(GRID):
            for col in range(GRID):
                pixel_slow = (frame + dither[row, col] * 3) // 61
                shifted = (dither[row, col] + frame * 3 + drift[row, col] * pixel_slow) % TOTAL
                if shifted >= action_threshold:
                    current[row, col] = sprite[row, col]
                else:
                    current[row, col, 3] = 0

                r, g, b, a = current[row, col]
                color = (int(r), int(g), int(b)) if a > 0 else SKY_BLUE
                pygame.draw.rect(
                    screen, color,
                    (col * SCALE, row * SCALE, SCALE, SCALE),
                )

        frame += 1

        pygame.display.flip()
        clock.tick(60)

        if frame % 30 == 0:
            fps = clock.get_fps()
            pygame.display.set_caption(f"R2 Dither Dissolve  |  {fps:.1f} fps")


if __name__ == "__main__":
    dither = build_dither_array()
    plot_dither(dither)

    sprite = build_sprite()
    dithered = apply_dithered_transparency(sprite, dither, transparency=0.25)

    pygame.init()
    show_debug_sprites(sprite, dithered)
    animate(sprite, dither, transparency=0.25)
    pygame.quit()
