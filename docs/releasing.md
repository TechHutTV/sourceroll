# Releasing

A release is a tag, a GitHub release with a built package attached, and an update of the `sourceroll` package on the AUR. Two workflows do most of the work; you decide when by publishing a draft.

## How it fits together

1. **Every merge to `master` updates a draft release.** The *Draft release* workflow names and tags the draft `v` plus the version in the source, and lists the pull requests merged since the last published release. Branches named `feature/...` are labelled `enhancement` and listed under New, `fix/...` are labelled `bug` and listed under Fixed, and everything else goes under Other changes. Add the `skip-changelog` label to leave a pull request out. Labels can also be set by hand.
2. **Publishing the draft releases it.** GitHub creates the tag at the tip of `master`, and the *Release* workflow starts. In a clean Arch Linux container it runs the tests, then builds the package from the tag's tarball with `aur/PKGBUILD`, the way an AUR user's machine would. It attaches the package to the release and pushes `PKGBUILD` and `.SRCINFO` to the AUR.

The same tests and package build run on every pull request as a rehearsal, from a tarball of that commit, so a broken recipe shows before a tag exists.

## One-time setup for the AUR

Until this is done, releases are still built and attached; the AUR step is skipped with a notice.

1. Create an account on [aur.archlinux.org](https://aur.archlinux.org).
2. Make a key for the workflow alone:

   ```sh
   ssh-keygen -t ed25519 -N '' -C 'sourceroll releases' -f aur_key
   ```

3. Add the contents of `aur_key.pub` to the AUR account under My Account, SSH Public Key.
4. Store the private half as a repository secret, then delete both files:

   ```sh
   gh secret set AUR_SSH_PRIVATE_KEY < aur_key
   rm aur_key aur_key.pub
   ```

The first push creates the `sourceroll` package on the AUR, owned by that account. Pre-releases are not pushed to the AUR.

## Cut a release

1. Raise the version in a pull request. It is written in four places: `src/main.cpp`, `plugin/manifest.json`, `pkgbuild/PKGBUILD`, and `aur/PKGBUILD`. Set `pkgrel` in `aur/PKGBUILD` back to 1. `bin/check-release` prints the version and fails when the copies disagree; the workflows run it too.
2. Merge it and wait for the *Draft release* run to finish, so the draft carries the new version.
3. Open the draft on the [releases page](https://github.com/TechHutTV/sourceroll/releases), edit the notes if you like, and publish it.
4. Watch the *Release* run. When it is green, `yay -S sourceroll` installs the new version.

After a release, merges do not start a new draft until the version is raised. Nothing is lost: the next draft lists everything merged since the last published release.

## When something goes wrong

- **The Release run failed after publishing.** Fix the cause, then start the workflow by hand: Actions, Release, Run workflow, with the tag, such as `v0.6.0`. It tests and uses the recipe of the branch it is started from, with the tarball of the tag, so the version on that branch must still be the tag's.
- **Only the recipe was wrong.** Raise `pkgrel` in `aur/PKGBUILD`, merge, and run the workflow by hand with the tag. The source and the tag stay as they are.
- **The AUR's host key changed.** The workflow pins it and will refuse to connect. Replace `AUR_HOST_KEY` in `.github/workflows/release.yml` with the new key, after checking its fingerprint against the one on the AUR home page.

## By hand

The workflows call these scripts, and they work from a checkout:

```sh
bin/check-release      # print the version; fail if its copies disagree
bin/aur-package        # write PKGBUILD and .SRCINFO for the released tag into build/aur
bin/aur-publish        # push them to the AUR with your own SSH key and Git identity
```

`bin/aur-publish` downloads the release tarball and compares its checksum before pushing, because a push to the AUR cannot be taken back.

To try the recipe before a version is tagged:

```sh
bin/aur-package --local
cd build/aur && makepkg -f
```

That builds a tarball of the current commit. `bin/aur-publish` refuses those trial files.
