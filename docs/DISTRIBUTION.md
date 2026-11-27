# Distribution

Pushing a `v*` tag runs `.github/workflows/release.yml`, which makes two installables:

| Platform | Installable | Updates |
|---|---|---|
| Ubuntu Touch | `kyauth.busnes_<version>_arm64.click` on the GitHub release | by installing the next release |
| Nura and Linux desktops | the signed Flatpak repository at <https://busnes-app.github.io/KyAuth-Linux>, and `kyauth-<arch>.flatpak` bundles on the release | `flatpak update`, from either |

The Flatpak is built for x86_64 and aarch64, each on a native runner, and every pull request
builds and smoke-tests both. A bundle carries the repository URL and signing key, so a bundle
install updates from the repository like a remote install does.

## Cutting a release

1. Bump `project(kyauth VERSION …)` in `CMakeLists.txt`.
2. Add a `<release version="…" date="…"/>` at the top of `<releases>` in
   `packaging/nura/app.busnes.kyauth.metainfo.xml`.
3. Merge, then tag: `git tag v<version> && git push origin v<version>`.

The workflow refuses a tag that does not match both, before anything is published: the
Flatpak repository is append-only.

## One-time setup

The publish job fails on a tag until these are done. Pull request builds need none of them.

1. **Signing key.** Run `packaging/flatpak/gen-signing-key.sh --upload`. It writes a
   passphraseless RSA-4096 key with a bounded 2-year expiry (`KEY_EXPIRE=2y`, configurable
   via `KEY_EXPIRE=<N>[d|w|m|y]`) to `~/kyauth-flatpak-signing-key/` and sets the
   `FLATPAK_GPG_PRIVATE_KEY` and `FLATPAK_GPG_KEY_ID` secrets. **Back up `private.asc`
   offline.** Secrets cannot be read back, and without the key no update can be signed for
   existing installs.
2. **First release.** Tag the first release. Its publish job creates the `gh-pages` branch.
3. **Pages.** Serve `gh-pages` from the root:
   `gh api -X POST repos/Busnes-app/KyAuth-Linux/pages -f 'source[branch]=gh-pages' -f 'source[path]=/'`

## Signing-key rollover (OSTree multi-key)

The publish job warns when an active signing key has no expiry or is within 90 days of
expiration, and refuses to publish with an expired key. Start rollover at least **90 days
before** expiry so installed clients pick up the new key during `flatpak update`:

1. **Generate the next key** into a separate directory:
   `OUT_DIR=~/kyauth-flatpak-signing-key-next packaging/flatpak/gen-signing-key.sh`
   Back up `~/kyauth-flatpak-signing-key-next/private.asc` offline.
2. **Stage dual-key overlap** (leave `FLATPAK_GPG_PRIVATE_KEY` and `FLATPAK_GPG_KEY_ID` intact):
   ```sh
   gh secret set FLATPAK_GPG_PRIVATE_KEY_NEXT --repo Busnes-app/KyAuth-Linux \
     < ~/kyauth-flatpak-signing-key-next/private.asc
   printf '%s' "$(cat ~/kyauth-flatpak-signing-key-next/fingerprint.txt)" \
     | gh secret set FLATPAK_GPG_KEY_ID_NEXT --repo Busnes-app/KyAuth-Linux
   ```
3. **Cut a release during the overlap window.** `.github/workflows/release.yml` signs every
   commit, summary, and bundle with both keys, concatenates both public keys into
   `_site/kyauth.gpg` / `.flatpakrepo`, and passes `--gpg-import=_site/kyauth.gpg` to
   `flatpak build-update-repo` so OSTree publishes `xa.gpg-keys`. Existing installs verify
   the summary with the old key and automatically import the new key on `flatpak update`.
4. **Promote the new key** after the overlap release has soaked (and before the old key expires):
   ```sh
   gh secret set FLATPAK_GPG_PRIVATE_KEY --repo Busnes-app/KyAuth-Linux \
     < ~/kyauth-flatpak-signing-key-next/private.asc
   printf '%s' "$(cat ~/kyauth-flatpak-signing-key-next/fingerprint.txt)" \
     | gh secret set FLATPAK_GPG_KEY_ID --repo Busnes-app/KyAuth-Linux
   gh secret delete FLATPAK_GPG_PRIVATE_KEY_NEXT --repo Busnes-app/KyAuth-Linux
   gh secret delete FLATPAK_GPG_KEY_ID_NEXT --repo Busnes-app/KyAuth-Linux
   ```
5. **Existing non-expiring keys:** Either rotate using steps 1–4 above, or set a bounded
   expiry on the offline backup (`gpg --quick-set-expire <FPR> 2y`) and re-upload
   `FLATPAK_GPG_PRIVATE_KEY` so the next publish refreshes `xa.gpg-keys`.

## How publishing works

The build jobs never see the key. Each exports an unsigned OSTree repository; the publish job
signs every commit as it merges both into the published repository
(`flatpak build-commit-from --gpg-sign`), because clients verify commits, not only the
summary. Debug refs are not published, to stay well under the 1 GB Pages limit.

The published repository must accumulate, since `flatpak update` needs the commit a user
already has. Each release clones `gh-pages`, recreates the empty OSTree directories git drops,
adds the new commits, prunes to the last three, and force-pushes a single orphan commit.

Rehearsed locally with a throwaway key: a remote install verifies, a bundle install updates
to a newer commit, and a client trusting another key is refused.
