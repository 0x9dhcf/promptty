# Releasing

This project uses a **tag-driven** release flow. Pushing a `vX.Y.Z` tag to
`origin` triggers `.github/workflows/release.yml`, which builds the project,
packages a source tarball, and publishes a GitHub Release with notes extracted
from `CHANGELOG.md`.

## Invariants

The workflow enforces these — violating any of them fails the release:

1. The tag name **must** match `PROJECT_VERSION` in `CMakeLists.txt`
   (i.e. tag `v0.2.0` requires `project(promptty VERSION 0.2.0 ...)`).
2. `CHANGELOG.md` **must** contain a `## [X.Y.Z]` section matching the tag.
3. The release build must pass.

## Step-by-step

For version `X.Y.Z`:

1. **Bump the version** in `CMakeLists.txt`:
   ```cmake
   project(promptty VERSION X.Y.Z LANGUAGES CXX)
   ```

2. **Update `CHANGELOG.md`**:
   - Rename the existing `## [Unreleased]` heading to `## [X.Y.Z] - YYYY-MM-DD`.
   - Add a fresh empty `## [Unreleased]` section above it.
   - Update the link references at the bottom of the file:
     ```markdown
     [Unreleased]: https://github.com/0x9dhcf/promptty/compare/vX.Y.Z...HEAD
     [X.Y.Z]: https://github.com/0x9dhcf/promptty/releases/tag/vX.Y.Z
     ```
   - (Keep the previous version's link reference too.)

3. **Commit** both files together:
   ```
   git add CMakeLists.txt CHANGELOG.md
   git commit -m "bump to X.Y.Z"
   ```

4. **Push main**:
   ```
   git push origin main
   ```

5. **Tag and push the tag**:
   ```
   git tag vX.Y.Z
   git push origin vX.Y.Z
   ```

6. **Watch the workflow**:
   ```
   gh run watch --exit-status
   ```
   or check the Actions tab on GitHub. On success, the release appears at
   `https://github.com/0x9dhcf/promptty/releases/tag/vX.Y.Z` with
   `promptty-X.Y.Z.tar.gz` attached.

## What the workflow does

`release.yml` runs these steps in order:

1. Checks out the tagged commit (full history — needed for `git archive`).
2. **Verifies the tag matches** `CMakeLists.txt` — fails loudly if they drift.
3. Installs GCC 15 and build dependencies.
4. Configures and builds the `release` preset.
5. Creates `promptty-X.Y.Z.tar.gz` via `git archive` (reproducible, excludes `.git`).
6. Extracts the `## [X.Y.Z]` section from `CHANGELOG.md` into `RELEASE_NOTES.md`.
   Fails if the section is missing.
7. Publishes the GitHub Release via `softprops/action-gh-release@v2`,
   attaching the tarball and using the extracted notes as the body.

## Recovering from a failed release

If the workflow fails (mismatched tag, missing changelog entry, build failure,
yaml bug, etc.), delete the tag **locally and on the remote**, fix the issue,
commit, and retag:

```
git push --delete origin vX.Y.Z
git tag -d vX.Y.Z
# ... fix things, commit if needed ...
git tag vX.Y.Z
git push origin vX.Y.Z
```

Do **not** try to edit or force-move a published tag that already has a
GitHub Release attached — delete the release first (`gh release delete vX.Y.Z`)
before re-tagging.

## Changelog conventions

`CHANGELOG.md` follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Entries are grouped under these headings, in this order:

- `### Added` — new features
- `### Changed` — changes to existing behavior (including build requirements)
- `### Deprecated` — soon-to-be-removed features
- `### Removed` — removed features
- `### Fixed` — bug fixes
- `### Security` — vulnerability fixes

Write entries in user-facing language — describe the effect, not the commit.
Skip pure chore/doc commits (gitignore tweaks, README polish) unless they
affect users. Squash related commits into a single bullet where it reads more
naturally.

This project follows [Semantic Versioning](https://semver.org/). While on
`0.x.y`, any release may contain breaking changes — document them clearly
in `### Changed` so downstream consumers are not surprised.
