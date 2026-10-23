// PspAndroidShims.cpp — the Android platform answers the PPSSPP subset
// needs, for an embedding that has no Android UI and no JNI storage layer.
//
// Upstream PPSSPP satisfies these symbols in android/jni/app-android.cpp,
// which is part of its full Android application (Activity, JNI storage
// callbacks, the works) and is deliberately NOT in the audited subset
// (see scripts/core-sources.sh). What remains is exactly the shape
// Core/psp_core.cpp already uses for its fail-closed shims:
//
//   * The content-URI storage API (Common/File/AndroidStorage.h). This
//     embedding hands the core real filesystem paths only (the SAF /
//     cache-copy decision lives in the session layer, not the core), so
//     content:// URIs never reach these functions in practice. Where a
//     stub branch exists in upstream's own header for non-Android
//     builds, these definitions return the same values it does.
//     Android_IsContentUri is the one honest implementation: it is a
//     pure scheme check, and callers dispatch on it.
//
//   * TextDrawerAndroid. The subset compiles the software GPU and its
//     PPGe atlas text path; the OS text drawer serves PPSSPP's own UI,
//     which a headless embedding never draws. Upstream's real drawer
//     renders through a Java TextRenderer over JNI; there is no such
//     class here, so this drawer reports not-ready and measures
//     nothing. (iOS ships the real Cocoa drawer because it is
//     self-contained; the Android one is not — it includes
//     android/jni/app-android.h.)
//
// If a later stage gives the app a real Android storage layer, these
// are the definitions to replace — search for this file's name.

#include "Common/File/AndroidStorage.h"
#include "Common/Render/Text/draw_text_android.h"

// ---------------------------------------------------------------------------
// Content-URI storage (Common/File/AndroidStorage.h)
// ---------------------------------------------------------------------------

void Android_StorageSetActivity(jobject /*nativeActivity*/) {}

bool Android_IsContentUri(std::string_view uri) {
	return uri.size() >= 10 && uri.compare(0, 10, "content://") == 0;
}

int Android_OpenContentUriFd(std::string_view /*uri*/, const Android_OpenContentUriMode /*mode*/) {
	return -1;
}

StorageError Android_CreateDirectory(const std::string & /*parentTreeUri*/, const std::string & /*dirName*/) {
	return StorageError::UNKNOWN;
}

StorageError Android_CreateFile(const std::string & /*parentTreeUri*/, const std::string & /*fileName*/) {
	return StorageError::UNKNOWN;
}

StorageError Android_MoveFile(const std::string & /*fileUri*/, const std::string & /*srcParentUri*/, const std::string & /*destParentUri*/) {
	return StorageError::UNKNOWN;
}

StorageError Android_CopyFile(const std::string & /*fileUri*/, const std::string & /*destParentUri*/) {
	return StorageError::UNKNOWN;
}

StorageError Android_RemoveFile(const std::string & /*fileUri*/) {
	return StorageError::UNKNOWN;
}

StorageError Android_RenameFileTo(const std::string & /*fileUri*/, const std::string & /*newName*/) {
	return StorageError::UNKNOWN;
}

bool Android_GetFileInfo(const std::string & /*fileUri*/, File::FileInfo * /*info*/) {
	return false;
}

bool Android_FileExists(const std::string & /*fileUri*/) {
	return false;
}

int64_t Android_ComputeRecursiveDirectorySize(const std::string & /*fileUri*/) {
	return -1;
}

int64_t Android_GetFreeSpaceByContentUri(const std::string & /*uri*/) {
	return -1;
}

int64_t Android_GetFreeSpaceByFilePath(const std::string & /*filePath*/) {
	return -1;
}

bool Android_IsExternalStoragePreservedLegacy() {
	return false;
}

const char *Android_ErrorToString(StorageError /*error*/) {
	return "";
}

std::vector<File::FileInfo> Android_ListContentUri(const std::string & /*uri*/, const std::string & /*prefix*/, bool *exists) {
	if (exists) {
		*exists = false;
	}
	return std::vector<File::FileInfo>();
}

// ---------------------------------------------------------------------------
// OS text drawer (Common/Render/Text/draw_text_android.h)
// ---------------------------------------------------------------------------

jobject TextDrawerAndroid::activity_ = nullptr;

TextDrawerAndroid::TextDrawerAndroid(Draw::DrawContext *draw)
	: TextDrawer(draw) {}

TextDrawerAndroid::~TextDrawerAndroid() {}

bool TextDrawerAndroid::IsReady() const {
	return false;
}

void TextDrawerAndroid::SetOrCreateFont(const FontStyle & /*style*/) {}

bool TextDrawerAndroid::DrawStringBitmap(std::vector<uint8_t> & /*bitmapData*/, TextStringEntry & /*entry*/, Draw::DataFormat /*texFormat*/, std::string_view /*str*/, int /*align*/, bool /*fullColor*/) {
	return false;
}

void TextDrawerAndroid::MeasureStringInternal(std::string_view /*str*/, float *w, float *h) {
	if (w) {
		*w = 0.0f;
	}
	if (h) {
		*h = 0.0f;
	}
}

void TextDrawerAndroid::ClearFonts() {}
