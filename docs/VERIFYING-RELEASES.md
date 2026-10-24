# Verifying releases

[Product overview](../README.md) · [Distribution](DISTRIBUTION.md) · [Assurance case](ASSURANCE-CASE.md)

> **Untested.** No KyAuth for Linux release has been published. The `v0.5.0` tag exists, but
> its release run failed at "Require the signing key": the Flatpak signing secrets are not
> set yet, so there is no signed repository, no GitHub release and no key. Nothing on this
> page has been run against a real release. It describes what `release.yml` will publish and
> how to check it once it does.

## What is signed, and by whom

`release.yml` runs on a `v*` tag and publishes two kinds of installables:

| Installable | Signed? | How you check it |
|---|---|---|
| The Flatpak repository at `https://busnes-app.github.io/KyAuth-Linux/repo/` | **Yes.** Every OSTree commit and the repository summary are GPG-signed in the publish job (`flatpak build-commit-from --gpg-sign`, `flatpak build-update-repo --gpg-sign`) | Flatpak checks the signature on every install and update |
| `kyauth-<arch>.flatpak` bundles on the GitHub release | **Yes**, with the same key, which the bundle also carries | Flatpak checks it on install, and on updates from the repository |
| `kyauth.busnes_<version>_arm64.click` on the GitHub release | **No.** Click packages installed from a file are not signed | Only that it came from this repository's release page over HTTPS |

The signing key is an RSA-4096 GPG key, "KyAuth Flatpak Signing Key
<kyauth-flatpak@busnes.app>", made by `packaging/flatpak/gen-signing-key.sh`. Only the
publish job can use it: the build jobs export unsigned repositories and never see the key.

## Check the signing key before you trust it

`kyauth.flatpakrepo` carries the public key, and `flatpak remote-add` trusts whatever key
it finds there. Before you add the remote, print the key's fingerprint and compare it
with one published somewhere else you trust:

```bash
curl -sSf https://busnes-app.github.io/KyAuth-Linux/kyauth.flatpakrepo \
  | sed -n 's/^GPGKey=//p' | base64 -d | gpg --show-keys --with-fingerprint
```

The repository does not publish the fingerprint anywhere yet. Until it does, this check
only tells you that the key and the repository came from the same GitHub Pages site.

## Install, and let Flatpak verify

```bash
flatpak remote-add --if-not-exists kyauth https://busnes-app.github.io/KyAuth-Linux/kyauth.flatpakrepo
flatpak remotes --columns=name,url | grep kyauth
flatpak install kyauth app.busnes.kyauth
flatpak info app.busnes.kyauth          # the Commit: line names the signed OSTree commit
```

Flatpak refuses an unsigned commit, or one signed by another key, on install and on every
`flatpak update`. A bundle install does the same, then updates from the repository.

## What this project cannot offer yet

- **A published key fingerprint.** It should go in the release notes, or in this file,
  once the key exists.
- **A signature on the click package,** or a checksum for it.
- **A link from a package to its build.** No build-provenance attestation is made, and
  Actions logs are kept for 90 days.
- **A repeatable build.** Neither the Flatpak nor the click has been checked to rebuild to
  the same bytes, and the CI checks neither.
