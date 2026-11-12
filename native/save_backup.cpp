// Saves outliving the app: the game's hdd folder lives in the app's own storage, which Android wipes on uninstall.
// A copy of it is kept in the Castlevania folder of the shared storage (all files access, the same folder the game's
// files are read from), brought back at the next start when the hdd folder has nothing in it.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>

#include "app/app.h"
#include "core/log.h"
#include "host.h"

namespace host {

namespace {

namespace fs = std::filesystem;
using namespace std::chrono_literals;

const fs::path kBackup = "/storage/emulated/0/Castlevania/saves_backup";
// the backup as it was before this run first wrote to it, kept only when it held more than the game's folder does
// (a run that started without the saves brought back must not leave the only good copy overwritten)
// what the game's folder held just before a manual restore replaced it
const fs::path kBeforeRestore = "/storage/emulated/0/Castlevania/saves_before_restore";
// set by a manual restore: the backup is left as it is until the app starts again, so the game still running (it holds
// its own idea of the saves) cannot write over it
std::atomic<bool> g_restored{false};

const fs::path kBackupPrev = "/storage/emulated/0/Castlevania/saves_backup_prev";

fs::path Hdd() { return app::ExeDir() / "hdd"; }

// the files under a folder: how many, and a number that changes when any is added, resized or rewritten
struct Listing {
    uint64_t files = 0;
    uint64_t sig = 0;
};

Listing List(const fs::path& root) {
    Listing l;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        std::error_code e;
        if (!it->is_regular_file(e)) continue;
        uint64_t size = it->file_size(e);
        auto time = it->last_write_time(e).time_since_epoch().count();
        l.files++;
        l.sig = l.sig * 1099511628211ull ^ (size + uint64_t(time) * 31 + std::hash<std::string>{}(it->path().string()));
    }
    return l;
}

// every file of from into to that is missing there or differs in size or time; returns the files copied, -1 when the
// destination cannot be written
int Mirror(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::create_directories(to, ec);
    if (ec) return -1;
    int copied = 0;
    for (fs::recursive_directory_iterator it(from, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        std::error_code e;
        fs::path rel = fs::relative(it->path(), from, e);
        if (e) continue;
        fs::path dest = to / rel;
        if (it->is_directory(e)) {
            fs::create_directories(dest, e);
            continue;
        }
        if (!it->is_regular_file(e)) continue;
        if (fs::exists(dest, e) && fs::file_size(dest, e) == it->file_size(e) &&
            fs::last_write_time(dest, e) == it->last_write_time(e))
            continue;
        fs::create_directories(dest.parent_path(), e);
        fs::copy_file(it->path(), dest, fs::copy_options::overwrite_existing, e);
        if (e) {
            LOGW("saves: %s: %s", dest.c_str(), e.message().c_str());
            continue;
        }
        fs::last_write_time(dest, fs::last_write_time(it->path(), e), e);
        copied++;
    }
    return copied;
}

// the files of from that to does not have yet; the ones it has are left alone
int CopyMissing(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    int copied = 0;
    for (fs::recursive_directory_iterator it(from, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        std::error_code e;
        if (!it->is_regular_file(e)) continue;
        fs::path rel = fs::relative(it->path(), from, e);
        if (e) continue;
        fs::path dest = to / rel;
        if (fs::exists(dest, e)) continue;
        fs::create_directories(dest.parent_path(), e);
        fs::copy_file(it->path(), dest, fs::copy_options::none, e);
        if (e) {
            LOGW("saves: %s: %s", dest.c_str(), e.message().c_str());
            continue;
        }
        fs::last_write_time(dest, fs::last_write_time(it->path(), e), e);
        copied++;
    }
    return copied;
}

}  // namespace

void RestoreSaves() {
    std::error_code ec;
    const bool reachable = fs::exists(kBackup, ec) && !ec;
    const Listing backup = List(kBackup);
    const Listing hdd = List(Hdd());
    LOGI("saves: backup %s (%llu files, %s), game folder %llu files", kBackup.c_str(),
         (unsigned long long)backup.files, reachable ? "reachable" : ec ? ec.message().c_str() : "not there",
         (unsigned long long)hdd.files);
    if (backup.files == 0) return;
    // an empty game folder gets the whole backup; one that has some files (anything written before this ran) only
    // the files it lacks, so what is there is never overwritten
    int n = hdd.files == 0 ? Mirror(kBackup, Hdd()) : CopyMissing(kBackup, Hdd());
    LOGI("saves: %d files brought back from %s", n, kBackup.c_str());
}

std::string SavesFolder() { return Hdd().string(); }

std::string BackupFolder() { return kBackup.string(); }

// the RESTORE SAVE button: the backup copied over the game's folder. -1 nothing to restore (no backup, or no access to
// it), -2 the game's folder cannot be written, else the files copied
int RestoreSavesNow() {
    if (List(kBackup).files == 0) return -1;
    Mirror(Hdd(), kBeforeRestore);
    int n = Mirror(kBackup, Hdd());
    LOGI("saves: restore by hand, %d files copied from %s", n, kBackup.c_str());
    if (n < 0) return -2;
    g_restored = true;
    return n;
}

void StartSaveBackup() {
    std::thread([] {
        Listing last;
        bool first = true;
        bool warned = false;
        for (;;) {
            if (g_restored) {
                std::this_thread::sleep_for(5s);
                continue;
            }
            Listing now = List(Hdd());
            // changed, and the same as a moment ago: a file the game is writing is not copied half written
            if (now.files > 0 && now.sig != last.sig) {
                std::this_thread::sleep_for(2s);
                Listing settled = List(Hdd());
                if (settled.sig == now.sig) {
                    if (first) {
                        std::error_code pe;
                        Listing old_backup = List(kBackup);
                        if (old_backup.files > settled.files && !fs::exists(kBackupPrev, pe)) {
                            LOGW("saves: the backup holds %llu files, the game %llu: keeping a copy in %s first",
                                 (unsigned long long)old_backup.files, (unsigned long long)settled.files,
                                 kBackupPrev.c_str());
                            Mirror(kBackup, kBackupPrev);
                        }
                    }
                    int n = Mirror(Hdd(), kBackup);
                    if (n < 0) {
                        if (!warned)
                            LOGW("saves: no backup, %s cannot be written: grant All files access to the app in Android's "
                                 "settings (Apps, the app, Permissions)",
                                 kBackup.c_str());
                        warned = true;
                    } else if (n > 0 || first) {
                        LOGI("saves: %d files copied to %s", n, kBackup.c_str());
                    }
                    first = false;
                    last = settled;
                }
            }
            std::this_thread::sleep_for(5s);
        }
    }).detach();
}

}  // namespace host
