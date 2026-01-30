Primary Contributors to Mamba's Footprint:

   Component                              │ Impact           │ Details                                                
   ---------------------------------------+------------------+--------------------------------------------------------
   **State dimension (D)**                │ Memory           │ Hidden state size per layer - determines expressiveness
   **Model depth**                        │ Memory + Compute │ Number of stacked Mamba blocks (typical: 24-48 layers)
   **Parameter count**                    │ Storage + Memory │ 130M to 3B+ parameters in production models
   **Selective SSM parameters (B, C, Δ)** │ Compute          │ Input-dependent projections computed per token

   Secondary Contributors:

   Component                    │ Impact  │ Details                                           
   -----------------------------+---------+---------------------------------------------------
   **Input/output projections** │ Compute │ Linear layers expanding/contracting dimensions
   **Convolution layers**       │ Memory  │ 1D convolutions for local context (kernel size ~4)
   **Activation functions**     │ Compute │ SiLU/Swish nonlinearities
   **Normalization**            │ Compute │ RMSNorm per block

   Complexity Comparison:

   Metric       │ Mamba                │ CfC/Reflexor           
   -------------+----------------------+------------------------
   Training     │ O(L) per iteration   │ O(1) per sample
   Inference    │ O(T) per step        │ O(1) closed-form
   Memory       │ O(D) state per layer │ O(N) for N nodes
   Typical size │ 130M - 3B params     │ ~2.5K params (50 nodes)

   Why CfC wins on edge: Mamba needs ~130M+ parameters to be competitive on language tasks. The Reflexor's 50-node CfC (~2.5K parameters) fits in L1 cache because it solves a narrower problem (anomaly detection) with a
   closed-form solution rather than learned sequence modeling.
