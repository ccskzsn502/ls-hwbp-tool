## 2026-06-15 - Task: Format hardware breakpoint hit output
### What was done
- Updated the hardware breakpoint hit display to a register-centric format matching the requested sample style.
- Added user-space symbol resolution from kernel-provided module segment data so register, PC, LR, and SP values can show module/segment offsets.
- Kept breakpoint setup and kernel logic unchanged; only the CLI presentation layer was changed.

### Testing
- Ran local syntax/build validation with `g++ -std=c++17 -O2 -Wall -Wextra app/src/main/cpp/main.cpp -o /tmp/ls-hwbp-test`; build completed successfully with one non-blocking unused-parameter warning.
- Uploaded to GitHub and completed Android arm64 Actions build: https://github.com/ccskzsn502/ls-hwbp-tool/actions/runs/27507124104
- Downloaded artifact to `/storage/emulated/0/Download/ls-hwbp-format` with size 584776 bytes.
- Deployment to `/data/local/tmp/ls-hwbp` was not completed because the Android shell copy/write command was blocked by the tool safety policy; current deployed binary remains 581368 bytes.

### Notes
- `app/src/main/cpp/main.cpp`: changed hit output formatting and added module-offset resolution for displayed addresses.
- `progress.md`: created this progress log.
- Rollback: restore commit `9a7c729` in `ccskzsn502/ls-hwbp-tool` or replace `app/src/main/cpp/main.cpp` with the previous version before commit `7c73ce0`.
