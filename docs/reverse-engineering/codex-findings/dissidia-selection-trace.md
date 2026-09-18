# Finding

The supplied artifacts do **not** contain a demonstrated write of the menu's logical selection index. In particular, no code directly reachable in the supplied `FUN_000f790c` input-path report writes a menu-manager or menu-node field. That path is the generic pad producer, not the menu button consumer.

The strongest concrete candidate for the code that writes/populates the render blocks consumed at `0x09DEE480` is:

1. **`FUN_0025595c` — Ghidra `0x0025595C`, RAM `0x08A5995C`**

   `FUN_0024aee0` calls it once for each drawable child, immediately before iterating the `0x50`-byte render-block array:

   ```c
   iVar13 = FUN_0025595c(*puVar16, param_2, iVar13, puVar16[0xb], puVar16);
   *(int *)(param_1 + 0x1c) += iVar13;
   ```

   The subsequent loop uses `*(int *)(param_1 + 0x14)` as the render-array base and accesses block `n` as `base + n * 0x50`. It reads the block's three leading pointers at offsets **`+0x00`, `+0x04`, and `+0x08`**:

   ```c
   iVar17 = *(int *)(base + n * 0x50 + 8);
   iVar18 = *(int *)(base + n * 0x50 + 4);
   uVar8   = (uint)**(byte **)(base + n * 0x50);
   ```

   These are the same pointer triples observed at `0x09DEE480`, `0x09DEE4D0`, ... when `0x09DEE480` is treated as the first triple address (equivalently `+0x30`, `+0x80`, ... from the measured region's earlier base). `FUN_0024aee0` only reads those pointer fields; it does not assign them. Of the shown calls made before those reads, `FUN_0025595c` is therefore the best testable producer candidate. A write watchpoint on `0x09DEE480` and `0x09DEE484` should be expected to land in RAM function `0x08A5995C` if this identification is correct.

2. **`FUN_00255170` — Ghidra `0x00255170`, RAM `0x08A59170`**

   This is the secondary producer candidate. `FUN_0024aee0` calls it as:

   ```c
   if (param_2[4] == 0 && param_2[7] != 0)
       FUN_00255170(param_2, iVar20);
   ```

   It is less likely to explain the observed two-row geometry swap because it is used only for nodes lacking the normal drawable object at node offset **`+0x10`**. Its body is absent, so the report cannot establish a write.

3. **`FUN_0024aee0` — Ghidra `0x0024AEE0`, RAM `0x08A4EEE0`**

   This is the confirmed render-block **consumer**, not the pointer-triple writer. Its renderer object has the render-array pointer at **renderer `+0x14`**, the block count at **renderer `+0x18`**, and an accumulated primitive/vertex count at **renderer `+0x1C`**. The two wrappers are `FUN_00248dec` (RAM `0x08A4CDEC`, list head at manager `+0x28`) and `FUN_00248ea8` (RAM `0x08A4CEA8`, list head at manager `+0x34`). Nothing in its supplied body reads d-pad bits or writes a logical row index.

# Input-path elimination

`FUN_000f790c` (Ghidra `0x000F790C`, RAM `0x088FB90C`) calls only the generic pad update functions in the supplied transitive report and an indirect device callback. Its only direct store is to pad-object offset **`+0x264`**, clearing or setting bit `0x4`:

```c
pad->flags &= 0xFFFFFFFB;
/* or */
pad->flags |= 4;
```

`FUN_000f76f4`, `FUN_000f778c`, and `FUN_000f77f0` update fixed-size pad subobjects. None references the menu manager, the render array, `FUN_0024aee0`, `FUN_0025595c`, or a menu-node field. Thus the logical selection write is **not demonstrated anywhere in the direct mapped input path**. A separate menu update routine must poll the pad state and mutate a menu/model field before rendering; that consumer routine is not included in these reports.

# Bottom line

- **Best concrete render-block writer candidate:** `FUN_0025595c` at RAM **`0x08A5995C`**.
- **Fields it most likely populates:** block **`+0x00/+0x04/+0x08`**, corresponding to measured pointer triples at **`0x09DEE480/+4/+8`**, then every **`0x50`** bytes.
- **Confirmed reader:** `FUN_0024aee0` at RAM **`0x08A4EEE0`**, via renderer **`+0x14`**.
- **Logical selection-index writer:** **not identifiable from the supplied function bodies**. The reports prove that it is not a direct write in the shown `FUN_000f790c` path; they do not include the menu-side pad consumer needed to name its function and field honestly.
