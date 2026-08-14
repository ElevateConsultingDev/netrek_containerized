# Netrek @ Elevate Constitution

Governing principles for the Netrek project at Elevate Consulting: a private
STURGEON game server, the **Netrek COM** (Client of Mac) client, and the public
documentation site at `netrek.elevateconsulting.dev`.

## Core Principles

### I. The Match Is the Product
Everything exists to get real games played — first Dave vs Ron, then whoever
Dave invites. A working, reachable, private STURGEON server and a client that
plays cleanly on a Mac outrank polish, features, and elegance. When trading off,
choose what a player at the keyboard actually feels: connect, fly, fight.

### II. Netrek COM Is the Canonical Mac Client
The SDL2 Mac client (Netrek COM — *Client Of Mac*, a modernized COW) is owned,
branded, and maintained here as the authoritative Mac Netrek client. It becomes
"de facto" by being the best-documented, most welcoming, canonical repo — not by
restriction. Dave holds copyright on the original Mac/SDL2 code; original COW /
Netrek code keeps its authors' copyright. Bug reports and PRs are actively
invited and made easy.

### III. Private by Default, Ephemeral by Design
The game server is never open to the public internet. Access is by IP allow-list
only (email-to-enroll for guests; auto-enroll for the operator). Compute is
ephemeral — started for sessions, stopped otherwise — with a stable Elastic IP
and DNS so it can be reached the same way every time. No standing open ports, no
secrets in the repo (macOS Keychain / local files only).

### IV. Documentation Is Authoritative and Code-Sourced
The public site is the source of truth for how the server behaves. Game-mechanics
docs (STURGEON costs, menus, weapons) are written from the **actual server
source**, not hearsay, so they match what runs. Third-party copyrighted material
is linked, not copied; freely-licensed docs we own (GPL COW manuals) may be
hosted with attribution intact. Everything is styled to one consistent identity.

### V. Respect the GPL Lineage
The client is a derivative of GPL COW and is therefore GPL (v2-or-later). That is
embraced, not fought: forks are permitted, contributions flow back under the same
license, copyright headers are honest about who wrote what. Copyleft is the
mechanism by which the canonical client stays canonical.

## Infrastructure & Technology Constraints

- **Cloud**: AWS profile `elevate`, region `us-west-2`. Ephemeral EC2 + Elastic
  IP + a single security group locked to allow-listed IPs. Managed via the
  `prod-*.sh` scripts; IDs live in `prod-env.sh`.
- **DNS/edge**: Cloudflare for `elevateconsulting.dev`. `sturgeon.*` is DNS-only
  (raw game protocol); `netrek.*` is Cloudflare-proxied (HTTPS for a `.dev`
  HSTS domain) fronting GitHub Pages.
- **Server**: containerized Netrek (`quozl/netrek` + STURGEON/NEWBIE), 1:1 port
  mapping on prod so UDP works natively.
- **Client**: COW + an SDL2 backend, built with Homebrew SDL2 on macOS. Client
  config (`~/.netrekrc`) requires `tryShort: off` for this server.
- **Public artifacts**: only the Netrek COM client source and the docs site are
  public (`ElevateConsultingDev/netrek-clients`); server infra IDs stay private.

## Development Workflow

- Commit in small, coherent chunks as work completes; keep diffs minimal and
  reuse existing patterns over adding new ones.
- **Verify from the artifact, not inference** — confirm a deploy, a served page,
  or a firewall rule by reading the actual result before claiming success.
- Diagnose bugs at the root (the shared function all callers route through), not
  the symptom the report names.
- Match surrounding code style; no unrequested abstractions or scope creep.

## Governance

This constitution guides decisions when priorities conflict; the ranked
principles above break ties (I outranks V). Amendments are made by editing this
file with a version bump and a one-line rationale. Anything that would open the
server publicly, publish secrets/infra IDs, or relicense the client away from GPL
requires explicit owner approval and is presumed denied otherwise.

**Version**: 1.0.0 | **Ratified**: 2026-08-13 | **Last Amended**: 2026-08-13
