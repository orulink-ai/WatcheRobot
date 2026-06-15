# Contributing

Thanks for helping improve WatcheRobot.

## Scope

Accepted source contributions in this repository are limited to:

- ESP32-S3 firmware
- STM32F103 firmware
- public hardware materials
- public docs and release process docs
- public flashing and validation tools

Do not submit app, server, or desktop source code here. Those components are distributed as release artifacts through GitHub Releases.

## Pull Requests

- Keep firmware changes scoped to the relevant firmware directory.
- Include build or test results in the PR body.
- Do not add release binaries to Git. Upload binaries to GitHub Releases during release work.
- Do not commit Wi-Fi credentials, API keys, signing keys, serial-port maps, or private machine paths.
- Hardware changes should target a versioned directory such as `hardware/V1.0.0/`.

## Commit Style

Use concise, descriptive commit messages. Chinese commit bodies are acceptable for detailed engineering context.
