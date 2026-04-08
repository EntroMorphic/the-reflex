# Synthesis: Projection-Aware Gate Bias

## Decision: Don't Build. Describe. Ship.

The LMM revealed that per-neuron discriminability-weighted bias is a sound idea with an unvalidated metric, an entrainment risk, and uncertain payoff — and the paper doesn't need it. The honest 2/3 result with a clear diagnosis is stronger than a speculative fix.

## What Goes in the Paper

Add to Section 5.1 (The Robust Result) or a new Section 5.6:

> **Why Seed B Regresses.** The current gate bias is per-pattern-group: 4 values, one for each TriX neuron group of 8 GIE neurons. Lowering the threshold for a group increases firing for all 8 neurons equally. Whether this increases LP pattern separation depends on whether the LP weight matrix W_f carries pattern-discriminative information in the columns corresponding to that group's GIE neurons. For seeds A and C, the LP projection happens to be discriminative in the directions amplified by the group bias. For seed B, it is not — the LP columns for the biased group carry common-mode information rather than pattern-specific information. The bias amplifies noise.
>
> The predicted fix is per-neuron discriminability-weighted bias: 32 values instead of 4, where each GIE neuron's bias is weighted by how much its LP projection contributes to pattern separation in MTFP-space. Neurons whose LP weight columns connect to pattern-discriminative LP outputs receive full bias; neurons whose columns connect to non-discriminative outputs receive zero. This concentrates the bias on directions that help and zeros it on directions that hurt. The metric can be computed from the fixed LP weight matrices and the accumulated MTFP means after the cold-start window. Implementation is deferred to future work; the current per-group mechanism is reported as-is.

## What Doesn't Go in the Paper

- The entrainment risk analysis (too speculative without data)
- The three disc metric candidates (readers don't need the decision process)
- The full LMM journal (that's for us, not reviewers)

## Action

1. Add the paragraph above to the Stratum 1 paper
2. Commit
3. Ship

## What Gets Built Later

After papers ship, in the Tier 2 engineering phase:
1. Implement per-neuron bias with MTFP-space disc metric
2. Run all 3 seeds
3. If seed B improves: publish as a follow-up note
4. If seed B entrains: investigate, adjust decay rate, retry
5. If no change: the disc metric is wrong, try magnitude-based disc
