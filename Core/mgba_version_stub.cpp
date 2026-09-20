/* mgba_version_stub.cpp — version symbols for the mGBA core subset.
 *
 * mGBA generates these from git at configure time (version.c). This repo
 * builds a fixed mGBA translation-unit subset with no configure step, so the
 * symbols are provided here instead. Only the updater/frontend reference
 * them; the emulator never reads them. Values identify the pinned revision
 * (see scripts/bootstrap-deps.sh); keep them in step when the pin moves.
 */
extern "C" {
const char* const gitCommit = "543a197582c30364584d773a974d7f991892fa43";
const char* const gitCommitShort = "543a197";
const char* const gitBranch = "master";
const int gitRevision = 1;
const char* const binaryName = "mgba";
} // extern "C"
