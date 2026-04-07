#!/usr/bin/env python3
"""
Generate publication figures for the Kinetic Attention paper.
Data from four silicon runs of TEST 14 on ESP32-C6FH4.
"""

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import numpy as np

OUTDIR = '/home/ztflynn/Projects/000-research/the-reflex/figures'

# Color palette
C_14A = '#4A90D9'    # blue — baseline
C_14C = '#E85D3A'    # red-orange — full bias
C_ISO = '#2ECC71'    # green — delayed onset
C_BG  = '#FAFAFA'

plt.rcParams.update({
    'font.size': 10,
    'font.family': 'sans-serif',
    'axes.facecolor': C_BG,
    'figure.facecolor': 'white',
    'axes.grid': True,
    'grid.alpha': 0.3,
    'axes.spines.top': False,
    'axes.spines.right': False,
})


# ═══════════════════════════════════════════════════════════════
#  DATA — Four silicon runs
# ═══════════════════════════════════════════════════════════════

# Mean Hamming per condition per run (n/16)
mean_ham = {
    '14A':     [0.7, 1.8, 1.2, 1.3],
    '14C':     [2.2, 4.3, 2.2, 3.2],
    '14C-iso': [5.5, 2.2, 3.0, 5.7],
}

# Per-pair Hamming — Run 3 (representative, all 4 patterns sampled)
ham_run3 = {
    '14A':     [[0,1,1,1],[1,0,0,2],[1,0,0,2],[1,2,2,0]],
    '14C':     [[0,2,1,4],[2,0,1,2],[1,1,0,3],[4,2,3,0]],
    '14C-iso': [[0,3,2,3],[3,0,1,4],[2,1,0,5],[3,4,5,0]],
}

# Per-group gate fires — Run 3
fires_run3 = {
    '14A':     [27328, 176385, 152360, 0],
    '14C':     [34744, 191985, 152784, 0],
    '14C-iso': [27392, 187560, 157672, 0],
}

# Bias trace — Run 4 (14C and 14C-iso, from instrumented capture)
# Format: (time_s, [g0, g1, g2, g3])
bias_14c = [
    (0,   [0, 0, 0, 0]),
    (26,  [0, 9, 7, 0]),
    (49,  [0, 6, 10, 0]),
    (74,  [0, 5, 2, 0]),
    (101, [0, 8, 7, 0]),
    (120, [0, 7, 6, 0]),  # estimated end
]
bias_iso = [
    (0,   [0, 0, 0, 0]),
    (27,  [0, 0, 0, 0]),
    (54,  [0, 0, 0, 0]),
    (81,  [0, 0, 2, 0]),
    (107, [0, 1, 3, 0]),
    (120, [0, 1, 2, 0]),  # estimated end
]

# Confusion matrix — Run 4 (14A condition)
confusion_14a = np.array([
    [80,   0,  0,  0],
    [ 0, 200, 94,  0],
    [ 0,   0, 81,  0],
    [ 1,   1,  0, 13],
])


# ═══════════════════════════════════════════════════════════════
#  FIGURE 2: Bias Trace — Two Panels
# ═══════════════════════════════════════════════════════════════

def fig2_bias_trace():
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(8, 4.5), sharex=True)

    group_colors = ['#E74C3C', '#3498DB', '#F39C12', '#9B59B6']
    group_labels = ['G0 (P0)', 'G1 (P1)', 'G2 (P2)', 'G3 (P3)']

    for ax, data, title in [
        (ax1, bias_14c, '14C (full bias)'),
        (ax2, bias_iso, '14C-iso (bias after 60s)')
    ]:
        times = [d[0] for d in data]
        for g in range(4):
            vals = [d[1][g] for d in data]
            ax.plot(times, vals, 'o-', color=group_colors[g],
                    label=group_labels[g], markersize=5, linewidth=1.5)
        ax.set_ylabel('Gate bias')
        ax.set_ylim(-1, 16)
        ax.set_title(title, fontsize=10, fontweight='bold', loc='left')
        ax.axhline(y=0, color='black', linewidth=0.5, alpha=0.3)

    # Vertical line at t=60s on bottom panel
    ax2.axvline(x=60, color='black', linewidth=1, linestyle='--', alpha=0.5)
    ax2.annotate('bias\nactivates', xy=(60, 12), fontsize=8,
                 ha='center', color='#555')

    ax2.set_xlabel('Time (seconds)')
    ax1.legend(loc='upper right', fontsize=8, ncol=2)

    fig.tight_layout()
    fig.savefig(f'{OUTDIR}/fig2_bias_trace.png', dpi=200, bbox_inches='tight')
    fig.savefig(f'{OUTDIR}/fig2_bias_trace.pdf', bbox_inches='tight')
    plt.close(fig)
    print('Figure 2: bias trace saved')


# ═══════════════════════════════════════════════════════════════
#  FIGURE 3: LP Divergence Bar Chart (4 runs)
# ═══════════════════════════════════════════════════════════════

def fig3_divergence_bars():
    fig, ax = plt.subplots(figsize=(7, 4))

    conditions = ['14A\n(no bias)', '14C\n(full bias)', '14C-iso\n(bias after 60s)']
    keys = ['14A', '14C', '14C-iso']
    colors = [C_14A, C_14C, C_ISO]

    x = np.arange(len(conditions))
    width = 0.18

    for run_idx in range(4):
        offsets = x + (run_idx - 1.5) * width
        vals = [mean_ham[k][run_idx] / 16 * 100 for k in keys]
        bars = ax.bar(offsets, vals, width * 0.9,
                      color=[colors[i] for i in range(3)],
                      alpha=0.4 + 0.15 * run_idx,
                      edgecolor='white', linewidth=0.5)
        # Label run number on first group
        if run_idx == 0:
            for bar, c in zip(bars, colors):
                pass  # just for color

    # Mean lines
    for i, k in enumerate(keys):
        m = np.mean(mean_ham[k]) / 16 * 100
        ax.plot([x[i] - 2*width, x[i] + 2*width], [m, m],
                color=colors[i], linewidth=2.5, zorder=5)
        ax.text(x[i] + 2.2*width, m, f'{m:.0f}%',
                fontsize=9, va='center', color=colors[i], fontweight='bold')

    ax.set_ylabel('Mean LP Divergence (% of maximum)')
    ax.set_xticks(x)
    ax.set_xticklabels(conditions)
    ax.set_ylim(0, 45)
    ax.set_title('LP Divergence Across Conditions (4 runs, same configuration)',
                 fontsize=11, fontweight='bold')

    # Legend for runs
    from matplotlib.lines import Line2D
    legend_elements = [
        mpatches.Patch(facecolor='gray', alpha=0.4 + 0.15*i, label=f'Run {i+1}')
        for i in range(4)
    ]
    legend_elements.append(Line2D([0], [0], color='gray', linewidth=2.5, label='Mean'))
    ax.legend(handles=legend_elements, loc='upper left', fontsize=8)

    fig.tight_layout()
    fig.savefig(f'{OUTDIR}/fig3_divergence_bars.png', dpi=200, bbox_inches='tight')
    fig.savefig(f'{OUTDIR}/fig3_divergence_bars.pdf', bbox_inches='tight')
    plt.close(fig)
    print('Figure 3: divergence bars saved')


# ═══════════════════════════════════════════════════════════════
#  FIGURE 4: Per-Pair Heatmaps (Run 3)
# ═══════════════════════════════════════════════════════════════

def fig4_heatmaps():
    fig, axes = plt.subplots(1, 3, figsize=(10, 3.2))

    titles = ['14A (no bias)', '14C (full bias)', '14C-iso (bias after 60s)']
    keys = ['14A', '14C', '14C-iso']
    labels = ['P0', 'P1', 'P2', 'P3']

    for ax, key, title in zip(axes, keys, titles):
        data = np.array(ham_run3[key])
        im = ax.imshow(data, cmap='YlOrRd', vmin=0, vmax=8,
                       aspect='equal')
        ax.set_xticks(range(4))
        ax.set_yticks(range(4))
        ax.set_xticklabels(labels)
        ax.set_yticklabels(labels)
        ax.set_title(title, fontsize=9, fontweight='bold')

        # Annotate cells
        for i in range(4):
            for j in range(4):
                v = data[i, j]
                color = 'white' if v > 4 else 'black'
                ax.text(j, i, str(v), ha='center', va='center',
                        fontsize=11, fontweight='bold', color=color)

    fig.colorbar(im, ax=axes, shrink=0.8, label='Hamming distance (0-16)')
    fig.suptitle('LP Divergence Matrix — Run 3 (representative)',
                 fontsize=11, fontweight='bold', y=1.02)
    fig.tight_layout()
    fig.savefig(f'{OUTDIR}/fig4_heatmaps.png', dpi=200, bbox_inches='tight')
    fig.savefig(f'{OUTDIR}/fig4_heatmaps.pdf', bbox_inches='tight')
    plt.close(fig)
    print('Figure 4: heatmaps saved')


# ═══════════════════════════════════════════════════════════════
#  FIGURE 5: Per-Group Gate Fire Rates (Run 3)
# ═══════════════════════════════════════════════════════════════

def fig5_fire_rates():
    fig, ax = plt.subplots(figsize=(7, 4))

    groups = ['G0\n(P0 neurons)', 'G1\n(P1 neurons)',
              'G2\n(P2 neurons)', 'G3\n(P3 neurons)']
    keys = ['14A', '14C', '14C-iso']
    colors = [C_14A, C_14C, C_ISO]
    labels = ['14A (no bias)', '14C (full bias)', '14C-iso (bias after 60s)']

    x = np.arange(4)
    width = 0.25

    for i, (key, color, label) in enumerate(zip(keys, colors, labels)):
        vals = [fires_run3[key][g] / 1000 for g in range(4)]
        ax.bar(x + (i - 1) * width, vals, width * 0.9,
               color=color, label=label, edgecolor='white')

    # Annotate G0 shift
    g0_14a = fires_run3['14A'][0] / 1000
    g0_14c = fires_run3['14C'][0] / 1000
    ax.annotate(f'+{(g0_14c/g0_14a - 1)*100:.0f}%',
                xy=(0, g0_14c), xytext=(0.5, g0_14c + 15),
                fontsize=9, fontweight='bold', color=C_14C,
                arrowprops=dict(arrowstyle='->', color=C_14C, lw=1.5))

    ax.set_ylabel('Gate fires (thousands)')
    ax.set_xticks(x)
    ax.set_xticklabels(groups)
    ax.set_title('Per-Group Gate Firing — Run 3',
                 fontsize=11, fontweight='bold')
    ax.legend(fontsize=8)

    fig.tight_layout()
    fig.savefig(f'{OUTDIR}/fig5_fire_rates.png', dpi=200, bbox_inches='tight')
    fig.savefig(f'{OUTDIR}/fig5_fire_rates.pdf', bbox_inches='tight')
    plt.close(fig)
    print('Figure 5: fire rates saved')


# ═══════════════════════════════════════════════════════════════
#  FIGURE 6 (BONUS): Confusion Matrix (Run 4)
# ═══════════════════════════════════════════════════════════════

def fig6_confusion():
    fig, ax = plt.subplots(figsize=(4.5, 3.8))

    labels = ['P0', 'P1', 'P2', 'P3']
    # Normalize rows to percentages
    row_sums = confusion_14a.sum(axis=1, keepdims=True)
    row_sums[row_sums == 0] = 1
    pct = confusion_14a / row_sums * 100

    im = ax.imshow(pct, cmap='Blues', vmin=0, vmax=100, aspect='equal')

    ax.set_xticks(range(4))
    ax.set_yticks(range(4))
    ax.set_xticklabels(labels)
    ax.set_yticklabels(labels)
    ax.set_xlabel('Predicted (core_pred)')
    ax.set_ylabel('Ground truth (sender)')
    ax.set_title('CPU Classification Confusion\n14A, Run 4',
                 fontsize=10, fontweight='bold')

    for i in range(4):
        for j in range(4):
            count = confusion_14a[i, j]
            p = pct[i, j]
            color = 'white' if p > 50 else 'black'
            txt = f'{count}\n({p:.0f}%)' if count > 0 else '0'
            ax.text(j, i, txt, ha='center', va='center',
                    fontsize=9, color=color)

    fig.colorbar(im, ax=ax, shrink=0.8, label='Row %')
    fig.tight_layout()
    fig.savefig(f'{OUTDIR}/fig6_confusion.png', dpi=200, bbox_inches='tight')
    fig.savefig(f'{OUTDIR}/fig6_confusion.pdf', bbox_inches='tight')
    plt.close(fig)
    print('Figure 6: confusion matrix saved')


# ═══════════════════════════════════════════════════════════════
#  FIGURE 1: System Architecture (text-based — Excalidraw later)
# ═══════════════════════════════════════════════════════════════

def fig1_architecture():
    """Generate a simplified architecture diagram using matplotlib."""
    fig, ax = plt.subplots(figsize=(10, 6))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 7)
    ax.axis('off')

    # Layer boxes
    boxes = [
        # (x, y, w, h, color, label, sublabel)
        (0.5, 4.8, 9, 1.5, '#E8F4FD', 'Layer 1: GIE (430 Hz, peripheral hardware)',
         'GDMA → PARLIO → PCNT → ISR\nTriX classification (100%) · CfC blend · gate bias applied here'),
        (0.5, 2.6, 9, 1.5, '#FDE8E8', 'Layer 2: LP Core (100 Hz, 16 MHz RISC-V, ~30 µA)',
         'Ternary CfC (16 neurons) · VDB (64-node NSW graph)\nCMD 5: CfC step → VDB search → feedback blend'),
        (0.5, 0.4, 9, 1.5, '#E8FDE8', 'Layer 3: HP Core (160 MHz, on demand)',
         'Init · ESP-NOW receive · CPU classification (~80%)\nAgreement computation · gate_bias[4] write'),
    ]

    for x, y, w, h, color, label, sublabel in boxes:
        rect = mpatches.FancyBboxPatch((x, y), w, h, boxstyle='round,pad=0.1',
                                        facecolor=color, edgecolor='#333', linewidth=1.5)
        ax.add_patch(rect)
        ax.text(x + 0.3, y + h - 0.35, label, fontsize=10, fontweight='bold',
                va='top')
        ax.text(x + 0.3, y + 0.3, sublabel, fontsize=8, va='bottom',
                color='#555', family='monospace')

    # Arrows
    arrow_style = dict(arrowstyle='->', color='#333', lw=2)
    phase5_style = dict(arrowstyle='->', color='#E85D3A', lw=2.5,
                        linestyle='--')

    # GIE → LP (hidden state)
    ax.annotate('', xy=(5, 4.8), xytext=(5, 4.1),
                arrowprops=arrow_style)
    ax.text(5.2, 4.45, 'gie_hidden[32]', fontsize=7, color='#555')

    # LP → HP (lp_hidden)
    ax.annotate('', xy=(5, 2.6), xytext=(5, 1.9),
                arrowprops=arrow_style)
    ax.text(5.2, 2.25, 'lp_hidden[16]', fontsize=7, color='#555')

    # Phase 5 feedback: HP → GIE (gate bias)
    ax.annotate('', xy=(8.5, 4.8), xytext=(8.5, 1.9),
                arrowprops=phase5_style)
    ax.text(8.7, 3.5, 'gate_bias[4]\n(Phase 5)', fontsize=8,
            color='#E85D3A', fontweight='bold', va='center')

    # Title
    ax.text(5, 6.7, 'The Reflex: Three-Layer Architecture with Phase 5 Feedback',
            fontsize=12, fontweight='bold', ha='center')

    fig.savefig(f'{OUTDIR}/fig1_architecture.png', dpi=200, bbox_inches='tight')
    fig.savefig(f'{OUTDIR}/fig1_architecture.pdf', bbox_inches='tight')
    plt.close(fig)
    print('Figure 1: architecture saved')


# ═══════════════════════════════════════════════════════════════
#  GENERATE ALL
# ═══════════════════════════════════════════════════════════════

if __name__ == '__main__':
    fig1_architecture()
    fig2_bias_trace()
    fig3_divergence_bars()
    fig4_heatmaps()
    fig5_fire_rates()
    fig6_confusion()
    print(f'\nAll figures saved to {OUTDIR}/')
