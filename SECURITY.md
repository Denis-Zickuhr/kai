# Security policy

## Reporting a vulnerability

Please report privately, not in a public issue: use **Security → Report a
vulnerability** on the GitHub repository
(<https://github.com/Denis-Zickuhr/kai/security/advisories/new>).

Include the version, your OS, and steps to reproduce. You'll get a reply as
soon as possible; this is a solo-maintained project, so allow a few days.

## Supported versions

Only the latest stable release and the current beta receive fixes.

## Scope

Kai runs commands and HTTP requests that the user writes. Running a
user-authored command is the intended behavior, not a vulnerability. Reports
about the app doing something the user did not ask for (network access,
file changes, leaking secrets or API keys) are in scope.
