# dm_core ztest

This test app exercises the DataModel core in isolation:

- snapshot initialization
- runtime and update field writes
- diagnostics state management
- request queue submit/receive paths

Build it as a standalone Zephyr app with `CONFIG_ZTEST=y`.