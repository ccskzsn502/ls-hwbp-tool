## 2026-06-15 - Task: Adjust hardware breakpoint register output formatting
### What was done
- Removed the leading `*` marker from resolved register values.
- Removed raw pointer dereference output for values that do not resolve to a module segment.
- Kept module offset output for resolved addresses, while continuing to show the full `X0-X29`, `LR`, `SP`, and `PC` register set.
- Rebuilt the Android arm64 CLI through GitHub Actions and deployed it to `/data/local/tmp/ls-hwbp`.

### Testing
- Ran local build validation with `g++ -std=c++17 -O2 -Wall -Wextra app/src/main/cpp/main.cpp -o /tmp/ls-hwbp-test`; build completed successfully with one existing non-blocking unused-parameter warning.
- GitHub Actions Android arm64 build completed successfully: https://github.com/ccskzsn502/ls-hwbp-tool/actions/runs/27525060625
- Downloaded artifact to `/storage/emulated/0/Download/ls-hwbp-cleanfmt` with size 584056 bytes.
- Deployed with `install -m 755` to `/data/local/tmp/ls-hwbp` and verified `/data/local/tmp/ls-hwbp ping` returned `驱动响应正常`.

### Notes
- `app/src/main/cpp/main.cpp`: adjusted only the register/stack presentation helpers; no breakpoint, kernel, or shared protocol logic was changed.
- `progress.md`: appended this task record.
- Rollback: redeploy the previous artifact from GitHub Actions run `27507124104`, or revert commit `066ff46` in `ccskzsn502/ls-hwbp-tool` and rebuild.

## 2026-06-15 - Task: Remove remaining register return marker
### What was done
- Removed the `[ret]` display marker from `X0` in hardware breakpoint hit output.
- Kept register values and module-offset resolution unchanged; only presentation markers were cleaned.
- Rebuilt and redeployed the Android arm64 CLI.

### Testing
- Verified `app/src/main/cpp/main.cpp` no longer contains `[ret]`, `read_target_u64`, raw dereference output, or leading-star output patterns.
- GitHub Actions Android arm64 build completed successfully: https://github.com/ccskzsn502/ls-hwbp-tool/actions/runs/27526016341
- Downloaded artifact to `/storage/emulated/0/Download/ls-hwbp-nomarker` with size 583880 bytes.
- Deployed to `/data/local/tmp/ls-hwbp` and `/data/local/tmp/ls-hwbp-cn`, then verified `/data/local/tmp/ls-hwbp ping` returned `驱动响应正常`.

### Notes
- `app/src/main/cpp/main.cpp`: removed the optional `ret` parameter from register output and changed `X0` printing to plain register output.
- `progress.md`: appended this task record.
- Rollback: redeploy the artifact from GitHub Actions run `27525060625`, or revert commit `441b4f2` in `ccskzsn502/ls-hwbp-tool` and rebuild.

## 2026-06-15 - Task: Simplify hwbp workflow and add memory write
### What was done
- Removed user-facing multi-breakpoint and module-list commands from the CLI and interactive menu.
- Kept the internal kernel module enumeration used for `so+offset` hit output and module-offset breakpoint address resolution.
- Added session-only target reuse in interactive mode without writing any config or cache files.
- Added interactive memory read and memory write flows, and added the CLI `write` command.

### Testing
- Ran local build validation with `g++ -std=c++17 -O2 -Wall -Wextra app/src/main/cpp/main.cpp -o /tmp/ls-hwbp-simplify-check`; build completed successfully with the existing unused `point` parameter warning.
- Verified source no longer exposes `cmd_modules`, `modules`, `multi`, `--point`, `--filter`, or `--regions` command/menu entry patterns.
- GitHub Actions Android arm64 build completed successfully: https://github.com/ccskzsn502/ls-hwbp-tool/actions/runs/27536415034
- Downloaded artifact to `/storage/emulated/0/Download/ls-hwbp-simplify` with size 581064 bytes.
- Deployed to `/data/local/tmp/ls-hwbp` and `/data/local/tmp/ls-hwbp-cn`; `/data/local/tmp/ls-hwbp ping` returned `驱动响应正常`, and `/data/local/tmp/ls-hwbp info` returned BRP/WRP counts.
- Verified removed `modules` and `multi` commands now show usage instead of executing removed functionality; verified `write` command entry is present and rejects missing bytes safely. No real target memory write was performed without a safe address.

### Notes
- `app/src/main/cpp/main.cpp`: simplified menu/CLI flow, removed multi-point and module-list entry points, added session-only target reuse, and added memory write support using existing `op_w`.
- `progress.md`: appended this task record.
- Rollback: redeploy the artifact from GitHub Actions run `27526016341`, or revert commit `8fe2293` in `ccskzsn502/ls-hwbp-tool` and rebuild.