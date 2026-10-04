# Getting started with an AI assistant and GitHub only: no local compiler

**Version:** 1.0.0
**Last Modified:** 2026-10-03
**Author:** UltraCanvas Framework

This is the companion to [`GettingStarted.md`](GettingStarted.md) for a
programmer who writes UltraCanvas applications **through an AI assistant and
GitHub alone**: Claude Code on the web, in the Claude app or in a cloud
session, with the repository on GitHub, and no C++ toolchain on the machine in
front of them. Every rule of the main guide still applies. What changes is
*where things happen*: the assistant's container edits and commits, GitHub
Actions compiles and tests on six platforms, and the built application comes
back as a download.

The short version: **the pull request is your compiler.** Nothing is built,
checked or versioned until a PR exists, so the PR is opened right after the
skeleton, not at the end, and the assistant watches it and fixes what CI
reports.

---

## Step 1 — Connect Claude Code to the repository

1. Install the Claude GitHub App on the repository (or ask an organisation
   owner to): <https://github.com/apps/claude/installations/select_target>.
   Without it the assistant can clone but not push, and cannot read or write
   pull requests.
2. Connect your GitHub account in the Claude app's settings, under Connectors.
3. Start a Claude Code session on the repository. The web app clones it into
   a fresh container; nothing is on your machine. The session reads
   `CLAUDE.md`, which sends it to `AGENTS.md`, the module registry and the
   docs map, so no further setup is needed for the assistant to know the
   conventions.
4. The committed `.claude/settings.json` applies in that container too: the
   `SessionStart` hook restates the delivery rule, the `Stop` hook refuses to
   end a turn silently on work that is not committed and pushed, and the
   read-only `git` and check-script commands are pre-approved. This matters
   more here than on a laptop, because **the container is discarded when the
   session ends.** An edit that was never pushed is gone, not pending.

**Other assistants and hosts.** Any agent that reads `AGENTS.md` and can push
to a branch fits the same workflow. GitLab, Bitbucket and the other hosts do
not: the checks, the six-platform build and the version folding are GitHub
Actions workflows under `.github/workflows/`, so on another host there is no
compiler until someone ports them.

## Step 2 — Understand what CI does for you

The Build workflow runs on every pull request and on every push to `main`. It
is the only place the whole framework is compiled for everyone:

| Leg | Platform | What it also does |
|---|---|---|
| ubuntu-22.04, ubuntu-22.04-arm | Linux, x86_64 and arm64, shared core | runs the unit tests, the multi-user database tests, installs the CMake package and builds `Tests/PackageConsumer` against it |
| macos-15, macos-15-intel | Apple Silicon and Intel | packages the app suite for macOS 15 and later; signs and notarises on `main` |
| windows-latest, windows-11-arm | MSYS2 CLANG64 and CLANGARM64 | packages the standalone zip |
| Android backend check | syntax check against the NDK | — |

Seven check workflows run beside it on every PR: the changelog guard, the
llms-txt sync, UI reuse, callback cycles, UTF-8 path strings, locale-decimal
numbers and Win32 names. Each is the CI form of a `scripts/check_*.py` script
the assistant can run in its container before pushing.

Three facts about CI decide how a cloud-only developer works:

- **A branch with no PR gets nothing.** The workflows trigger on
  `pull_request` and on pushes to `main`, not on pushes to a feature branch.
  A draft PR counts. The Build workflow can also be started by hand against a
  branch from the Actions tab (`workflow_dispatch`).
- **Every leg uploads the packaged application as a workflow artifact**, kept
  for seven days: `UCDemo-Windows-<version>-<arch>`,
  `UCDemo-MacOS-<version>-<arch>` and `UltraCanvas-Linux-<version>-<arch>`.
  That download is how you run and look at the app without compiling it.
  The macOS one is a disk image holding an `UltraCanvas` folder: copy the
  whole folder to Applications, because its apps share the `Frameworks/`
  inside it and do not start when moved out on their own.
- **A missing changelog entry fails CI before anything builds.** A framework
  change needs a file under `Docs/UltraCanvas/changelog.d/`; an application
  change needs a new top line in `Docs/<App>/CHANGELOG.md`. The number is
  assigned on `main` after the merge, never on the branch.

## Step 3 — The first session: skeleton, changelog, pull request

Ask for the skeleton and the PR in the same session, because nothing can be
verified until the PR exists:

> Create `Apps/MyApp` modelled on `Apps/UltraAuthenticator`: a headless core
> library plus a GUI target, `--version` from `Docs/MyApp/CHANGELOG.md`
> through `cmake/UltraCanvasVersion.cmake`, the version in the window title,
> an icon under `media/appicon/`, and the `option()` block in the root
> `CMakeLists.txt`. One window with an `UltraCanvasToolbar` and an
> `UltraCanvasSplitPane`. Read `AGENTS.md` and
> `Docs/UltraCanvas/UltraCanvasUIElements.md` first. Add the changelog, run
> the check scripts, commit, push, and open a draft pull request titled
> `MyApp: application skeleton with toolbar and split pane`.

The assistant can compile in its container when the environment's network
policy lets it install the Ubuntu packages from `GettingStarted.md` step 1; it
cannot show a window, since the container has no display. So the first real
proof is CI, and the first look at the app is the artifact.

When the assistant opens the PR it renames the chat to start with the PR
number, and it can **watch the PR**: CI failures and review comments wake the
session, and it fixes and pushes or explains what is blocking. Ask for that
explicitly ("watch the PR and fix CI") if it does not offer.

## Step 4 — The working loop

Every change after the skeleton follows the same loop. One bounded change per
session; the assistant does the typing, GitHub does the building, you do the
looking.

1. **Ask for one change**, naming the elements and the docs to read, as the
   prompt recipes in `GettingStarted.md` do.
2. **The assistant edits, runs the check scripts, commits and pushes** to the
   PR branch. The `Stop` hook stops it from ending a turn with unpushed work.
3. **CI builds and tests.** Six platforms, the checks, the package consumer.
   Ten to twenty-five minutes.
4. **The assistant reads the result.** A red leg is its job: it reads the
   job log, reproduces what it can in the container, fixes and pushes. A
   review comment from you or a bot is handled the same way.
5. **You download the artifact and run the app.** Describe what you see in
   the next prompt, or attach a screenshot. This is the only step the
   assistant cannot do for you.
6. **Read the `## Delivery` block** at the end of every reply. It says what
   is committed, what is pushed and which PR carries it. *Uncommitted* or *no
   pull request* means nothing has reached anyone yet.

Two things to say to the assistant that save a round trip each:

- "Before pushing, run the build and every `scripts/check_*.py` in your
  container, then push once." One validated push beats three speculative
  ones, and each push costs a full CI matrix.
- "If CI is red, read the log and fix it before reporting back." A reply that
  describes a failure without a pushed fix is a wasted turn.

## Step 5 — Review and merge

- Review the PR on GitHub as you would any other. Small asks ("rename this",
  "add a test for that") the assistant implements and pushes; larger ones it
  proposes first.
- **Do not merge while the session may still push.** A merged PR cannot
  receive commits. Merge after the session says it is done, or tell it so it
  restarts from `main` and opens a fresh PR for the rest.
- Merge with the PR title as the commit message, not the branch name.
  Session branches have random names (`claude/busy-ptolemy-iy5fog`) that mean
  nothing in `git log`.
- After the merge, `main` builds once more and the changelog fold assigns the
  framework version. The next session starts from that `main`.

## Step 6 — When the session ends

A session can end at any time: you close it, it times out, the container is
reclaimed. Before that happens, the work must be in the PR. The hooks and the
`## Delivery` block exist for this; read the block. If a session did end with
work unpushed, it is lost, and the next session starts the change again from
the PR's last commit.

A new session on the same branch picks up where the PR is. Tell it the PR
number and what is left, or point it at the `## Next Task` block of the last
reply.

## What you do not need

- A C++ compiler, CMake or any library on your machine.
- A clone of the repository. GitHub's web editor is enough for the odd typo
  fix, and the assistant handles everything else.
- The platform-specific steps of `GettingStarted.md` (toolchains, packaging
  scripts). CI runs them.

## What you still need

- The main guide's rules, because the assistant is held to them: elements
  from the catalogue, UTF-8 paths, dot-decimal numbers, no Win32 macro names,
  a changelog entry per change, docs for new APIs.
- A machine that can run the artifact for the platform you ship to, to look
  at the application. A screenshot from the assistant does not exist in this
  workflow.
- Patience with the CI cycle. The matrix is the compiler, and it takes
  minutes, not seconds, so bundle a prompt's worth of change per push.

## Checklist

- [ ] Claude GitHub App installed on the repository; GitHub connected in the Claude app
- [ ] First session: skeleton, changelog entry, checks run, pushed, draft PR open
- [ ] The assistant is watching the PR
- [ ] Every change: one prompt, one validated push, CI green, artifact downloaded and looked at
- [ ] `## Delivery` read at the end of every reply; nothing left uncommitted
- [ ] Merged with the PR title, only after the session said it was done
