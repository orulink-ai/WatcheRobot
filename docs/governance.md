# Governance

`orulink-ai/WatcheRobot` is the public repository for open firmware, hardware materials, public docs, and release coordination.

Maintainers should keep the following boundaries clear:

- firmware source changes belong under `firmware/`
- hardware source changes belong under `hardware/Vx.y.z/`
- app, server, and desktop binaries are release assets only
- release artifacts are uploaded to GitHub Releases
- private implementation details do not belong in public docs

Branch protection should require CI and owner review before public release.
