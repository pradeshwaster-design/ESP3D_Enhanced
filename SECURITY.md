# Security Policy

## Supported Versions

| Version | Supported |
|---------|-----------|
| 3.0.x   | ✅        |

## Reporting a Vulnerability

If you discover a security vulnerability in ESP3D-Ender3, please report it responsibly:

1. **Do not** open a public GitHub issue for security vulnerabilities.
2. Contact the repository maintainer directly through GitHub or the project's contact information.
3. Include details about the vulnerability, affected versions, and steps to reproduce.
4. Allow reasonable time for a response and fix before public disclosure.

## Security Best Practices for Users

- **Never commit real Wi-Fi credentials.** Use `myconfig.h` (git-ignored) or placeholders in `configuration.h`.
- **Do not expose the printer-control web interface directly to the public internet.** Use it only on your local network or behind a VPN.
- **Authentication is optional** and provides guest/user/admin levels. Passwords are stored in clear text — do not rely on it for public-facing security.
- **Keep firmware and dependencies updated.** Review the upstream ESP3D repository for security patches.
- **Review configuration before sharing.** Remove passwords, tokens, and IP addresses from config files before publishing.
- **Printer safety:** Disconnect or secure the printer before testing potentially dangerous motion commands. Wireless control can move axes unexpectedly.

## What We Do Not Claim

This project does not claim:
- HTTPS support
- Encrypted password storage
- Two-factor authentication
- Advanced access control beyond what is documented in the source code

## Disclosure Policy

Security fixes will be released in a new version with release notes describing the fix. We appreciate responsible disclosure and will credit reporters who wish to be acknowledged.