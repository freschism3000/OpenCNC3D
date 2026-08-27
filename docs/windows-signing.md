# Windows code signing, SmartScreen, and what it would actually cost

Written to answer one player report and one decision that only the project owner can make.

The report is "the game won't launch on Windows 11 from Smart App, it will test it itself but
it takes a while until it happens so a Cert would help it". Every new Windows player meets a
blue "Windows protected your PC" box, or an antivirus quarantine, before they see a frame.
Some of them stop there.

This file is the research behind the decision, so it can be made once with real numbers
rather than re-litigated. **Nothing in it is implemented.** What shipped alongside it is the
free half: the download's READ-ME now tells players how to get past the warning, and the
executable now carries a version resource so a report can say which build it came from.

---

## 1. What the warning actually is

Microsoft Defender SmartScreen checks a downloaded executable against a reputation service.
A file it has never seen, from a publisher it has never seen, gets the warning. It is not a
detection of anything; it is an absence of history.

Two facts decide everything below:

- **Reputation attaches to a file hash, and to the signing certificate.** An unsigned build
  has only the first, so reputation restarts from zero on **every single build**. This project
  ships a new number every round, so an unsigned build can never accumulate anything. A signed
  build restarts its file reputation too, but the certificate's reputation carries across
  builds, and that is the part that eventually stops the warning.
- **Signing does not switch the warning off.** It starts a clock that can finish. Unsigned,
  there is no clock.

### The thing that changed, and it changes the recommendation

Extended Validation certificates used to buy **instant** SmartScreen reputation. That was the
entire reason they cost several times an ordinary certificate. **Microsoft removed that
behaviour in 2024.** EV-signed files now build reputation on the same terms as OV-signed ones.

So the expensive option no longer buys the thing people bought it for. Anyone reaching for a
several-hundred-a-year EV certificate to fix this today is paying 2023 prices for a 2023
benefit that no longer exists.

---

## 2. The options, with costs

### Option A: do nothing, and tell players how to get past it. FREE. Shipped.

The warning can be removed by the player in three clicks, and it is worth knowing that the
best version of this is not "click Run anyway":

> Right click the downloaded **.zip**, Properties, tick **Unblock**, OK, and *then* extract.

That clears the Mark of the Web from the archive, so every file extracted from it is
unblocked at once and the game simply starts. Doing it after extraction means doing it per
file, or clicking through More info and Run anyway each build.

This is now in `READ-ME-WINDOWS.txt` in the download, written for a player rather than for a
developer. It costs nothing and it converts an apparently broken download into a documented
one step. It does not remove the warning, and for a player who does not read READ-ME files it
changes nothing.

### Option B: Azure Artifact Signing, about 10 a month. THE RECOMMENDATION.

Microsoft's own signing service, called Trusted Signing until it was renamed **Azure Artifact
Signing**. Generally available in the USA, Canada and Europe since January 2026, and open to
**individual developers**, not only companies, which is the part that used to block small
projects.

- Basic plan **9.99 USD a month**, up to 5,000 signatures, 0.005 each beyond that. The
  Premium plan at 99.99 a month is for volumes this project will never reach.
- Certificates are short lived and the private key never leaves Microsoft's service, so there
  is no hardware token to own, insure or lose. That is a real operational saving over a
  traditional certificate, independent of price.
- It requires an Azure subscription and an identity validation step. Budget days, not hours,
  for validation the first time.

**Two caveats, stated plainly because they are the ones that get glossed over:**

1. It does **not** grant instant SmartScreen trust. Signed builds still build reputation. What
   you are buying is the clock, plus a real publisher name in the warning instead of "Unknown
   publisher", plus not being quarantined by as many antivirus products.
2. At least one report exists of the 9.99 plan needing a paid Entra ID licence to configure
   roles. Verify that on the way in, because it changes the monthly number.

### Option C: a traditional OV certificate from a commercial CA

Roughly 200 to 400 a year, and since the 2023 storage rules the key has to live on a hardware
token or a cloud HSM, which is a shipping delay on the way in and a thing to keep safe
afterwards. Same reputation model as Option B, meaningfully more expensive, more to
administer. There is no reason to prefer it here unless Azure validation refuses.

### Option D: an EV certificate

Roughly 400 to 700 a year. **Do not.** See section 1: the instant-reputation behaviour that
justified the premium was removed in 2024, so this is Option C at a higher price.

---

## 3. If Option B is chosen, what the work is

Not costed in detail, because it should not be started before the account exists.

- **Signing happens on the Mac**, because that is where both platforms are cross compiled and
  `tools/release.sh` is one command by rule 9. Microsoft's signing client is Windows oriented,
  so the open question to settle first is whether a PE can be signed from macOS against Azure
  Artifact Signing directly, or whether the release has to hand the two executables to a
  Windows step. `osslsigncode` is the usual way to sign a PE from a Unix host and it is the
  first thing to test. **Settle this before buying anything**: if signing cannot be driven
  from the release script, the rule that a build is one command is what pays for it.
- **What gets signed**: `C&C3D.exe`, and `TiberianDawn.dll` beside it. `cnc_eyes.exe` is the
  verification binary the gate suite drives and nobody double clicks it, so it does not need
  to be signed and signing it would cost a signature per build for nothing.
- **Where it goes**: after `tools/win/build-win.sh` links and before `make-build-win.sh` zips.
  Signing after zipping signs nothing.
- **The gate**: a check that the shipped executable carries a valid signature, failing the
  release if it does not. A signing step that silently no-ops is exactly the class of green
  light rule 7 exists to refuse, and it would be invisible until a player reported the warning
  again.

---

## 4. What is NOT worth doing

- **A manifest for its own sake.** Adding an application manifest was considered and dropped.
  An `asInvoker` execution level would stop Windows' installer-name heuristics guessing this
  needs elevation, but the executable is called `C&C3D.exe` and trips no such heuristic. A DPI
  awareness declaration would be a real change to how the window behaves on a scaled display,
  and it cannot be tested from a machine that does not run Windows. Adding an untested
  behaviour change to fix a warning it does not fix is the wrong trade. Registered here rather
  than left as a silent omission.
- **Asking players to disable SmartScreen or add an exclusion.** It teaches exactly the habit
  that makes the next real warning ineffective.

---

## 5. Sources

- Trusted Signing opens to individual developers:
  https://techcommunity.microsoft.com/blog/microsoft-security-blog/trusted-signing-is-now-open-for-individual-developers-to-sign-up-in-public-previ/4273554
- Azure Artifact Signing product and pricing:
  https://azure.microsoft.com/en-us/products/artifact-signing
- SmartScreen reputation for Windows app developers:
  https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation
- Code signing options for Windows app developers:
  https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options
- EV certificates no longer grant immediate reputation:
  https://www.todesktop.com/blog/posts/windows-apps-psa-ev-certs-do-not-grant-immediate-reputation-anymore
- The Entra ID licence question on the Basic plan:
  https://learn.microsoft.com/en-us/answers/questions/5595324/i-signed-up-to-generate-certificates-to-sign-my-co
