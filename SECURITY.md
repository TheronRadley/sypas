# Security policy

## Supported source line

Security fixes are made on the current development line in this repository.
There are no supported tagged binary releases yet; do not rely on an
unpublished `-dev` image for security-sensitive use.

## Reporting a vulnerability

Please **do not open a public issue** for a suspected vulnerability in the
bootloader, kernel, image tooling, or release process. Use GitHub's private
security-advisory reporting flow for this repository, including:

- affected revision and configuration;
- a minimal reproducer or proof of concept;
- expected and observed behaviour; and
- any suggested mitigation, if available.

A report will be acknowledged as soon as possible, triaged privately, and a
fix and disclosure plan coordinated with the reporter before public details
are published.

## Scope note

SYPAS is an early experimental operating system. Its UEFI loader and kernel
are not a security boundary suitable for production deployment yet.
