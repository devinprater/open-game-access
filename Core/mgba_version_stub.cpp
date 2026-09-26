/* mgba_version_stub.cpp — version symbols for the mGBA core subset.
 *
 * mGBA generates these from git at configure time (version.c). This repo
 * builds a fixed mGBA translation-unit subset with no configure step, so the
 * symbols are provided here instead. Only the updater/frontend reference
 * them; the emulator never reads them. Values identify the pinned revision
 * (see scripts/bootstrap-deps.sh); keep them in step when the pin moves.
 */
// NOTE: every symbol needs an explicit `extern`. In C++ a const namespace-scope
// variable has INTERNAL linkage by default (unlike C, where version.c lives),
// so without it this object exports nothing and the app link fails with
// undefined _binaryName/_projectVersion/... from the mGBA objects.
extern "C" {
extern const char* const gitCommit = "543a197582c30364584d773a974d7f991892fa43";
extern const char* const gitCommitShort = "543a197";
extern const char* const gitBranch = "master";
extern const int gitRevision = 1;
extern const char* const binaryName = "mgba";
extern const char* const projectName = "mGBA";
extern const char* const projectVersion = "0.11-1-543a197";
} // extern "C"
