// The game on Android: a disc image (.iso) the launcher's picker chose, opened by its descriptor with no storage
// permission, or its files or an image in the app's own assets folder (what the launcher's folder import fills). The xbe must be the one this build was recompiled from; the Europe xbe
// is turned into the US one by src/common/europe.patch, linked into the library.
#include <jni.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_system.h>

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <fstream>

#include "app/app.h"
#include "common/options.h"
#include "core/log.h"
#include "image_symbols.h"
#include "kernel/crypto.h"
#include "kernel/disc.h"

extern const char kImageSha1[];  // the xbe recompiled into this library, written by CMake

// src/common/europe.patch, EUROPE_PATCH as CMake names it
__asm__(".section .rodata\n"
        ".global kEuropePatch\n"
        ".global kEuropePatchEnd\n"
        ".balign 16\n"
        "kEuropePatch:\n"
        ".incbin \"" EUROPE_PATCH "\"\n"
        "kEuropePatchEnd:\n"
        ".previous\n");
extern "C" const uint8_t kEuropePatch[];
extern "C" const uint8_t kEuropePatchEnd[];

namespace app {

namespace {

namespace fs = std::filesystem;

// the shared storage's folder for the game, readable with all files access: the files or the image in it, or in its
// assets folder
constexpr const char* kShared = "/storage/emulated/0/Castlevania";
constexpr const char* kVolumes = "/storage";

// the Black folders CodActivity has from Android's storage volumes, which some phones don't show in /storage
std::vector<fs::path> ActivityCardFolders() {
    std::vector<fs::path> out;
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return out;
    jclass cls = env->GetObjectClass(activity);
    jmethodID m = env->GetMethodID(cls, "cardFolderPaths", "()[Ljava/lang/String;");
    auto paths = m ? static_cast<jobjectArray>(env->CallObjectMethod(activity, m)) : nullptr;
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        paths = nullptr;
    }
    for (jsize i = 0, n = paths ? env->GetArrayLength(paths) : 0; i < n; i++) {
        auto s = static_cast<jstring>(env->GetObjectArrayElement(paths, i));
        if (!s) continue;
        const char* c = env->GetStringUTFChars(s, nullptr);
        if (c) {
            out.emplace_back(c);
            env->ReleaseStringUTFChars(s, c);
        }
        env->DeleteLocalRef(s);
    }
    if (paths) env->DeleteLocalRef(paths);
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return out;
}

// the Black folder on each SD card or USB stick Android has mounted, /storage/<volume id>/Black; all files access
// reads them as it does the phone's own storage
std::vector<fs::path> RemovableFolders() {
    std::vector<fs::path> out = ActivityCardFolders();
    std::error_code ec, dir_ec;
    for (auto& e : fs::directory_iterator(kVolumes, ec)) {
        std::string name = e.path().filename().string();
        if (name == "emulated" || name == "self" || !e.is_directory(dir_ec)) continue;
        out.push_back(e.path() / "Castlevania");
    }
    static bool logged = false;
    if (ec && !logged) {
        LOGW("game: %s cannot be listed: %s", kVolumes, ec.message().c_str());
        logged = true;
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// the path the launcher chose (game_path): a folder with the xbe, or an image / xbe file, tried before the usual folders
fs::path ChosenPath() { return fs::path(game::Opt().game_path); }

// an image the launcher's picker chose is a content:// document, which has no path to open
bool IsDocument(const std::string& s) { return s.rfind("content://", 0) == 0; }

// the document's file descriptor from the content resolver: the launcher kept the permission for its URI, and it is the
// same app. Detached from Java, so the caller owns it and closes it; -1 when it cannot be opened
int OpenDocument(const std::string& uri_text) {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return -1;
    std::vector<jobject> refs{activity};
    auto keep = [&](auto* o) {
        if (o) refs.push_back(o);
        return o;
    };
    // a Java exception: written to the log, then cleared so the next JNI call is allowed
    auto failed = [&] {
        if (!env->ExceptionCheck()) return false;
        env->ExceptionDescribe();
        env->ExceptionClear();
        return true;
    };
    int fd = -1;
    jclass activity_cls = keep(env->GetObjectClass(activity));
    jclass uri_cls = keep(env->FindClass("android/net/Uri"));
    jclass resolver_cls = keep(env->FindClass("android/content/ContentResolver"));
    jclass pfd_cls = keep(env->FindClass("android/os/ParcelFileDescriptor"));
    if (!failed() && activity_cls && uri_cls && resolver_cls && pfd_cls) {
        jmethodID get_resolver = env->GetMethodID(activity_cls, "getContentResolver", "()Landroid/content/ContentResolver;");
        jmethodID parse = env->GetStaticMethodID(uri_cls, "parse", "(Ljava/lang/String;)Landroid/net/Uri;");
        jmethodID open = env->GetMethodID(resolver_cls, "openFileDescriptor",
                                          "(Landroid/net/Uri;Ljava/lang/String;)Landroid/os/ParcelFileDescriptor;");
        jmethodID detach = env->GetMethodID(pfd_cls, "detachFd", "()I");
        if (!failed() && get_resolver && parse && open && detach) {
            jstring text = keep(env->NewStringUTF(uri_text.c_str()));
            jstring mode = keep(env->NewStringUTF("r"));
            jobject uri = keep(env->CallStaticObjectMethod(uri_cls, parse, text));
            jobject resolver = keep(env->CallObjectMethod(activity, get_resolver));
            if (!failed() && uri && resolver) {
                // a document that is gone, or whose permission the system took back, throws here
                jobject pfd = keep(env->CallObjectMethod(resolver, open, uri, mode));
                if (!failed() && pfd) {
                    fd = env->CallIntMethod(pfd, detach);
                    if (failed()) fd = -1;
                }
            }
        }
    }
    for (jobject r : refs) env->DeleteLocalRef(r);
    return fd;
}

std::vector<fs::path> AssetFolders() {
    std::vector<fs::path> out;
    if (const fs::path chosen = ChosenPath(); !chosen.empty() && !IsDocument(chosen.string())) {
        std::error_code ec;
        out.push_back(fs::is_directory(chosen, ec) ? chosen : chosen.parent_path());
    }
    out.push_back(fs::path(kShared));
    out.push_back(fs::path(kShared) / "assets");
    for (const fs::path& card : RemovableFolders()) {
        out.push_back(card);
        out.push_back(card / "assets");
    }
    out.push_back(ExeDir() / "assets");
    return out;
}

std::string Sha1(const std::vector<uint8_t>& data) {
    alignas(8) uint8_t ctx[128];
    uint8_t digest[20];
    crypto::ShaInit(ctx);
    crypto::ShaUpdate(ctx, data.data(), data.size());
    crypto::ShaFinal(ctx, digest);
    std::string hex;
    for (uint8_t b : digest) {
        hex += "0123456789abcdef"[b >> 4];
        hex += "0123456789abcdef"[b & 15];
    }
    return hex;
}

bool ReadHostFile(const fs::path& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

constexpr char kEuropeSha1[] = "0000000000000000000000000000000000000000";

class PatchReader {
public:
    PatchReader(const uint8_t* data, size_t size) : at_(data), end_(data + size) {}
    bool Done() const { return at_ == end_; }
    bool Failed() const { return failed_; }

    uint8_t Byte() {
        if (at_ == end_) {
            failed_ = true;
            return 0;
        }
        return *at_++;
    }

    // 7 bits a byte, low first
    uint64_t Number() {
        uint64_t n = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            uint8_t b = Byte();
            n |= uint64_t(b & 0x7F) << shift;
            if (b < 0x80) return n;
        }
        failed_ = true;
        return 0;
    }

private:
    const uint8_t* at_;
    const uint8_t* end_;
    bool failed_ = false;
};

// europe.patch applied to the Europe xbe, block by block
struct EuropePatch {
    const std::vector<uint8_t>& from;
    PatchReader patch;
    uint64_t read = 0;
    std::vector<uint8_t> out;

    // bytes copied from the Europe xbe with a few of them changed, new bytes, then a move of the read position
    bool ApplyBlock() {
        uint64_t copy = patch.Number();
        if (read > from.size() || copy > from.size() - read) return false;
        size_t start = out.size();
        out.insert(out.end(), from.begin() + read, from.begin() + read + copy);
        uint64_t changes = patch.Number();
        uint64_t at = 0;
        for (uint64_t i = 0; i < changes && !patch.Failed(); i++) {
            at += patch.Number();
            if (at >= copy) return false;
            out[start + at] += patch.Byte();
            at++;
        }
        uint64_t added = patch.Number();
        for (uint64_t i = 0; i < added && !patch.Failed(); i++) out.push_back(patch.Byte());
        return MoveRead(read + copy) && !patch.Failed();
    }

    // even moves forward by half, odd back by half plus one
    bool MoveRead(uint64_t from_end) {
        uint64_t move = patch.Number();
        uint64_t distance = move >> 1;
        if (distance > from.size()) return false;
        if (!(move & 1)) {
            read = from_end + distance;
            return true;
        }
        if (distance >= from_end) return false;
        read = from_end - distance - 1;
        return true;
    }
};

// the US xbe in place of the Europe one; false leaves it as it was
bool ConvertEurope(std::vector<uint8_t>& xbe) {
    size_t size = size_t(kEuropePatchEnd - kEuropePatch);
    if (!size) return false;
    EuropePatch europe{xbe, PatchReader(kEuropePatch, size)};
    while (!europe.patch.Done())
        if (!europe.ApplyBlock()) return false;
    xbe = std::move(europe.out);
    return true;
}

std::string Utf8(const wchar_t* w) {
    std::string s;
    for (; w && *w; w++) s += *w < 0x80 ? char(*w) : '?';
    return s;
}

// an image cut short (a copy that stopped, a bad download) still opens, its folders being near the start; the game
// then fails a read later and shows its dirty disc screen. Its files are checked against its size, for the log
void CheckImage(const kernel::Disc& disc, const std::string& name_of_image, uint64_t size) {
    uint64_t end = 0;
    size_t files = 0, past = 0;
    std::string first;
    std::vector<std::pair<const kernel::Disc::Entry*, std::string>> todo{{disc.Find(std::string_view()), ""}};
    while (!todo.empty()) {
        auto [dir, path] = todo.back();
        todo.pop_back();
        if (!dir) continue;
        for (const kernel::Disc::Entry& e : dir->children) {
            std::string name = path.empty() ? e.name : path + "\\" + e.name;
            if (e.dir) {
                todo.push_back({&e, name});
                continue;
            }
            files++;
            uint64_t file_end = e.offset + e.size;
            end = std::max(end, file_end);
            if (file_end > size && past++ == 0) first = name;
        }
    }
    LOGR("Game: image %llu bytes, %zu files up to byte %llu", (unsigned long long)size, files, (unsigned long long)end);
    if (past)
        LOGE("game: %s is cut short: %zu of its files end past its %llu bytes, %s the first; copy the image again",
             name_of_image.c_str(), past, (unsigned long long)size, first.c_str());
}

void CheckImage(const kernel::Disc& disc, const fs::path& iso) {
    std::error_code ec;
    uint64_t size = fs::file_size(iso, ec);
    if (ec) {
        LOGE("game: %s: its size cannot be read: %s", iso.string().c_str(), ec.message().c_str());
        return;
    }
    CheckImage(disc, iso.string(), size);
}

// no game yet: the activity's dialog tells the player where it goes and asks for the access; this waits a moment
// before looking again, keeping SDL's events moving
void WaitForGame() {
    static bool logged = false;
    if (!logged) {
        std::error_code ec;
        fs::is_directory(kShared, ec);
        std::string cards;
        for (const fs::path& card : RemovableFolders()) cards += ", " + card.string();
        LOGE("game: no game files in %s%s or %s%s, waiting for them", kShared, cards.c_str(),
             (ExeDir() / "assets").c_str(), ec == std::errc::permission_denied ? ", no access to the first" : "");
        logged = true;
    }
    for (int i = 0; i < 10; i++) {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_TERMINATING) {
                logx::Flush();
                std::_Exit(0);
            }
        SDL_Delay(100);
    }
}

// the xbes of other releases the player chose to always start, one sha1 a line, so the question is not asked again
fs::path AllowedFile() { return ExeDir() / "other_releases.txt"; }

bool AllowedRelease(const std::string& sha) {
    std::ifstream f(AllowedFile());
    for (std::string line; std::getline(f, line);)
        if (line == sha) return true;
    return false;
}

void AllowRelease(const std::string& sha) {
    std::ofstream f(AllowedFile(), std::ios::app);
    f << sha << "\n";
    if (!f) LOGW("game: %s cannot be written", AllowedFile().string().c_str());
}

bool AskOtherRelease(const fs::path& name, const std::string& sha) {
    if (AllowedRelease(sha)) {
        LOGR("Game: another release, started without asking as the player chose");
        return true;
    }
    std::string text = name.filename().string() +
                       " is a different release of the game than this build was made from, and will most likely "
                       "crash.\n\nStart it anyway?";
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "No"},
        {0, 1, "Yes"},
        {0, 2, "Yes, don't ask again"},
    };
    std::string title = Utf8(kGame.short_title);
    SDL_MessageBoxData box{SDL_MESSAGEBOX_WARNING, nullptr, title.c_str(), text.c_str(), 3, buttons, nullptr};
    int answer = 0;
    if (!SDL_ShowMessageBox(&box, &answer)) return false;
    if (answer == 2) AllowRelease(sha);
    return answer >= 1;
}

// 1 found, 0 none there, -1 the player turned down another release
int Search(GameFiles& game) {
    // the first copy with this xbe wins, the extracted files before any image; another release is kept to offer
    GameFiles other;
    std::unique_ptr<kernel::Disc> other_disc;
    fs::path other_name;
    std::string other_sha;
    auto consider = [&](const fs::path& assets, const fs::path& name, std::vector<uint8_t> xbe,
                        std::unique_ptr<kernel::Disc> disc) {
        std::string sha = Sha1(xbe);
        bool europe = sha == kEuropeSha1;
        if (europe) europe = ConvertEurope(xbe);
        std::string image_sha = europe ? Sha1(xbe) : sha;
        bool match = image_sha == kImageSha1;
        const char* release = europe ? ", Europe release" : "";
        LOGR("Game: %s in %s, sha1 %s%s%s", IMAGE_XBE, name.string().c_str(), sha.c_str(), release,
             match ? "" : ", another release");
        if (match) {
            game = {assets, std::move(xbe)};
            kernel::MountDisc(std::move(disc));
            return true;
        }
        if (other_name.empty()) {
            other = {assets, std::move(xbe)};
            other_disc = std::move(disc);
            other_name = name;
            other_sha = sha;
        }
        return false;
    };

    // the image the launcher's picker chose, a content:// document: by its descriptor, no storage permission needed
    if (const std::string chosen = game::Opt().game_path; IsDocument(chosen)) {
        int fd = OpenDocument(chosen);
        if (fd < 0) {
            LOGE("game: %s cannot be opened; choose the image again in the launcher", chosen.c_str());
        } else {
            struct stat st {};
            uint64_t size = fstat(fd, &st) == 0 ? uint64_t(st.st_size) : 0;
            auto disc = std::make_unique<kernel::Disc>();
            std::string error;
            std::vector<uint8_t> xbe;
            bool opened = disc->Open("fd:" + std::to_string(fd), error);  // the disc duplicates the descriptor
            close(fd);
            if (!opened) {
                LOGW("game: %s: %s", chosen.c_str(), error.c_str());
            } else if (!disc->Load(IMAGE_XBE, xbe)) {
                LOGW("game: %s has no %s", chosen.c_str(), IMAGE_XBE);
            } else {
                CheckImage(*disc, chosen, size);
                if (consider(ExeDir(), chosen, std::move(xbe), std::move(disc))) return 1;
            }
        }
    }

    std::error_code ec;
    for (const fs::path& assets : AssetFolders()) {
        if (!fs::is_directory(assets, ec)) continue;
        std::vector<uint8_t> xbe;
        if (ReadHostFile(assets / IMAGE_XBE, xbe) && consider(assets, assets, std::move(xbe), nullptr)) return 1;
        std::vector<fs::path> isos;
        for (auto& e : fs::directory_iterator(assets, ec)) {
            std::string ext = e.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (ext != ".iso" || !e.is_regular_file(ec)) continue;
            // a chosen image file: only it, not the others beside it
            const fs::path chosen = ChosenPath();
            if (assets == chosen.parent_path() && fs::is_regular_file(chosen, ec) && e.path() != chosen) continue;
            isos.push_back(e.path());
        }
        std::sort(isos.begin(), isos.end());
        for (const fs::path& iso : isos) {
            auto disc = std::make_unique<kernel::Disc>();
            std::string error;
            if (!disc->Open(iso, error)) {
                LOGW("game: %s: %s", iso.string().c_str(), error.c_str());
                continue;
            }
            if (!disc->Load(IMAGE_XBE, xbe)) {
                LOGW("game: %s has no %s", iso.string().c_str(), IMAGE_XBE);
                continue;
            }
            CheckImage(*disc, iso);
            if (consider(assets, iso, std::move(xbe), std::move(disc))) return 1;
        }
    }

    if (other_name.empty()) return 0;
    if (!AskOtherRelease(other_name, other_sha)) return -1;
    game = std::move(other);
    kernel::MountDisc(std::move(other_disc));
    return 1;
}

}  // namespace

bool FindGame(GameFiles& game) {
    for (;;) {
        int found = Search(game);
        if (found) return found > 0;
        WaitForGame();
    }
}

}  // namespace app
