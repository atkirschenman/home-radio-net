# Vendored third-party code (lib/rnet)

Copied as plain files, unmodified. To update: re-copy from the new revision and change this table in the same commit.

| Directory | Upstream | Revision | Files taken | Licence |
|---|---|---|---|---|
| `tinycrypt/` | github.com/zephyrproject-rtos/tinycrypt (Zephyr's maintained fork of intel/tinycrypt v0.2.8, which is archived) | `1012a3e` ("Fix warnings reported by UBSAN": v0.2.8's key schedule shifts into the sign bit, caught by our sanitizer build) | `lib/source/{aes_encrypt,cmac_mode,utils}.c`, `lib/include/tinycrypt/{aes,cmac_mode,constants,utils}.h` | BSD-3-Clause, Intel (`tinycrypt/LICENSE`, `tinycrypt/README.zephyr`) |
