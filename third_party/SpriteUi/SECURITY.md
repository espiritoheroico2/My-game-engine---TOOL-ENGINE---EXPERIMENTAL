# Security policy

## Reporting a vulnerability

Please do not report security-sensitive issues in a public issue or pull request.

If private vulnerability reporting is enabled for this repository, use the repository's **Security → Report a vulnerability** flow. Otherwise, contact the repository maintainers privately through GitHub and ask for a secure reporting channel before sending technical details.

Include the affected revision, impact, prerequisites and a minimal safe reproduction when possible. Do not include real user data, credentials or secrets. We will acknowledge reports and coordinate a fix and disclosure timeline with the reporter.

## Scope

SpriteUI is currently a CPU-side C++20 core with a null rendering backend. Security reports are welcome for the parser/configuration handling, memory safety, unsafe assumptions in public APIs, build/release infrastructure and any integrations added to the project. Third-party dependencies and future integrations should be identified separately where possible.

There is no guaranteed response-time SLA. Please allow maintainers reasonable time to investigate before public disclosure.
