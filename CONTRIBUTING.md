# Contributing to ESP3D-Ender3

Thank you for considering a contribution to ESP3D-Ender3! This is a community-enhanced fork of the [ESP3D](https://github.com/luc-github/ESP3D) project focused on wireless connectivity for 3D printers.

## How to Contribute

### 1. Set Up the Development Environment

1. Fork the repository on GitHub.
2. Clone your fork:

   ```bash
   git clone https://github.com/your-username/ESP3D-Enhanced.git
   cd ESP3D-Enhanced
   ```

3. Install [PlatformIO Core](https://docs.platformio.org/en/latest/core/install.html) or PlatformIO IDE for VS Code.
4. Create a branch for your change:

   ```bash
   git checkout -b feature/your-feature-name
   ```

### 2. Build and Test Changes

```bash
# Build the default target
platformio run

# Build and upload to your board
platformio run -e esp32_4MB_wifi -t upload

# Monitor serial output
platformio device monitor -p COM6 -b 115200
```

Before submitting, verify:
- The firmware compiles without errors for the default target.
- You have not changed pin assignments, flash partitions, or communication protocols without a clear reason.
- You have not committed any Wi-Fi passwords, tokens, or private keys.

### 3. Code Formatting

- Follow the existing code style in the source files (C/C++, Arduino/PlatformIO conventions).
- Use 4 spaces for indentation (no tabs).
- Keep comments concise and explanatory.
- Do not reformat unrelated code in the same pull request.

### 4. Reporting Bugs

Use the [bug report template](.github/ISSUE_TEMPLATE/bug_report.md) when filing an issue. Include:

- ESP3D version (from `info.json`)
- Board model and flash size
- Printer model and firmware (e.g., Marlin 2.x)
- Build environment (PlatformIO version)
- Steps to reproduce
- Relevant serial monitor output or build logs
- Configuration excerpts (with passwords removed)

**Do not share** real Wi-Fi credentials, API keys, or private network information.

### 5. Submitting Pull Requests

1. Push your branch to your fork.
2. Open a pull request against the `3.0` branch of this repository.
3. Use the [pull request template](.github/PULL_REQUEST_TEMPLATE.md).
4. Describe what changed, why, and how to test it.
5. Link to any related issues.

### 6. Documenting Hardware-Specific Changes

If your change affects a specific board or printer:
- Add the hardware to the compatibility matrix in README.md.
- Note whether it is tested or expected to work.
- Include wiring details (GPIO pins, baud rate, etc.) in the documentation.

## License

By contributing, you agree that your contributions will be licensed under the [LGPL v2.1](LICENSE), the same license as the ESP3D project.

## Code of Conduct

Please follow the [Code of Conduct](CODE_OF_CONDUCT.md) in all interactions.