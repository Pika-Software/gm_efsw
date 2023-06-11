#include <GarrysMod/Lua/Interface.h>
#include <efsw/efsw.hpp>
#include <memory>
#include <queue>
#include <mutex>
#include <unordered_map>
#include <filesystem.h>
#include <tier0/dbg.h>
#include <GarrysMod/InterfacePointers.hpp>
#include <GarrysMod/Lua/LuaInterface.h>
#include <optional>
#include <algorithm>

namespace Path {
    constexpr size_t PATH_BUFFER_SIZE = MAX_UNICODE_PATH;
    char* GetPathBuffer() {
        thread_local static char pathBuffer[PATH_BUFFER_SIZE];
        return pathBuffer;
    }

    void FixSlashes(std::string& path) { std::replace(path.begin(), path.end(), '\\', '/'); }
    void LowerCase(std::string& path) { std::transform(path.begin(), path.end(), path.begin(), ::tolower); }
    void Normalize(std::string& path) {
        FixSlashes(path);
        LowerCase(path);
    }
    bool RemoveDotSlashes(std::string& path) {
        return V_RemoveDotSlashes(&path[0], '/');
    }

    void StripFileName(std::string& path) {
        auto namePos = path.find_last_of('/');
        if (namePos != std::string::npos) path.erase(namePos);
    }
    std::string Join(std::string_view path, std::string_view subPath) {
        std::string finalPath = std::string(path);
        if (!finalPath.empty() && finalPath.back() != '/')
            finalPath += '/';
        finalPath += subPath;
        return finalPath;
    }

    std::string RelativeToFullPath(const std::string& path, const char* pathID) {
        bool success = g_pFullFileSystem->RelativePathToFullPath(path.c_str(), pathID, GetPathBuffer(), PATH_BUFFER_SIZE) != NULL;
        return success ? std::string(GetPathBuffer()) : std::string{};
    }
    std::string FullToRelativePath(const std::string& fullPath, const char* pathID) {
        bool success = g_pFullFileSystem->FullPathToRelativePathEx(fullPath.c_str(), pathID, GetPathBuffer(), PATH_BUFFER_SIZE);
        return success ? std::string(GetPathBuffer()) : std::string{};
    }
    std::string TransverseRelativePath(const std::string& relativePath, const char* fromPathID, const char* toPathID) {
        std::string result = RelativeToFullPath(relativePath, fromPathID);
        if (!result.empty()) result = FullToRelativePath(result, toPathID);
        return result;
    }
}

struct FileWatchEvent {
    efsw::Action action;
    efsw::WatchID watchid;
    std::string filename;
};
struct WatchMetadata {
    efsw::WatchID id;
    std::string directory;
    int watchCount;
};
class UpdateListener;

IFileSystem* g_pFullFileSystem = nullptr;
std::unique_ptr<efsw::FileWatcher> g_FileWatcher;
std::unique_ptr<UpdateListener> g_UpdateListener;
std::queue<FileWatchEvent> g_FileWatchEvents;
std::mutex g_FileWatchEventsLock;
std::unordered_map<efsw::WatchID, WatchMetadata> g_WatchMetadatas;
GarrysMod::Lua::ILuaInterface* g_Lua = nullptr;

class UpdateListener : public efsw::FileWatchListener {
public:
    void handleFileAction(efsw::WatchID watchid, const std::string& dir,
        const std::string& filename, efsw::Action action,
        std::string oldFilename) override 
    {
        if (action == efsw::Actions::Moved)
            return;

        FileWatchEvent event = { action, watchid, filename };
        Path::Normalize(event.filename);

        std::lock_guard<std::mutex> lock(g_FileWatchEventsLock);
        g_FileWatchEvents.push(std::move(event));
    }
};

const char* PathID(const char* pathID) {
    if (strcmp(pathID, "LUA") == 0) {
        return g_Lua->GetPathID();
    }
    return pathID;
}

std::optional<efsw::WatchID> FindWatchID(std::string_view directory) {
    for (const auto& [id, metadata] : g_WatchMetadatas) {
        if (metadata.directory == directory)
            return id;
    }
    return std::nullopt;
}

namespace LuaFuncs {
    LUA_FUNCTION(Think) {
        if (g_FileWatchEvents.empty())
            return 0;

        LUA->GetField(GarrysMod::Lua::INDEX_GLOBAL, "hook");
        if (!LUA->IsType(-1, GarrysMod::Lua::Type::Table))
            return 0;

        LUA->GetField(-1, "Run");
        if (!LUA->IsType(-1, GarrysMod::Lua::Type::Function))
            return 0;

        std::lock_guard<std::mutex> lock(g_FileWatchEventsLock);
        while (!g_FileWatchEvents.empty()) {
            FileWatchEvent& event = g_FileWatchEvents.front();

            auto result = g_WatchMetadatas.find(event.watchid);
            if (result != g_WatchMetadatas.end()) {
                auto& metadata = result->second;
                std::string path = Path::FullToRelativePath(Path::Join(metadata.directory, event.filename), "garrysmod");
                Path::Normalize(path);

                LUA->Push(-1);
                LUA->PushString("FileWatchEvent");
                LUA->PushNumber(event.action);
                LUA->PushNumber(event.watchid);
                LUA->PushString(path.c_str());
                if (LUA->PCall(4, 0, 0) != 0) {
                    Warning("[efsw] failed to call hook.Run: %s\n", LUA->GetString(-1));
                    LUA->Pop();
                }
            }

            g_FileWatchEvents.pop();
        }

        LUA->Pop(2);

        return 0;
    }

    LUA_FUNCTION(Watch) {
        std::string fileName = LUA->CheckString(1);
        const char* pathID = PathID(LUA->CheckString(2));

        if (!Path::RemoveDotSlashes(fileName)) {
            LUA->PushNumber(efsw::Errors::FileOutOfScope);
            return 1;
        }

        std::string fullPath = Path::RelativeToFullPath(fileName, pathID);
        Path::Normalize(fullPath);

        if (!fullPath.empty())
            Path::StripFileName(fullPath);

        if (fullPath.empty()) {
            LUA->PushNumber(efsw::Errors::FileNotFound);
            return 1;
        }

        auto findResult = FindWatchID(fullPath);
        if (findResult) {
            auto& metadata = g_WatchMetadatas.at(*findResult);
            metadata.watchCount++;
            LUA->PushNumber(metadata.id);
            return 1;
        }

        auto watchID = g_FileWatcher->addWatch(fullPath, g_UpdateListener.get(), false);
        if (watchID >= 0) {
            auto metadata = WatchMetadata{watchID, fullPath, 1};
            g_WatchMetadatas[watchID] = std::move(metadata);
        }
        
        LUA->PushNumber(watchID);
        return 1;
    }

    LUA_FUNCTION(Unwatch) {
        efsw::WatchID watchID = LUA->CheckNumber(1);
        auto result = g_WatchMetadatas.find(watchID);
        if (result != g_WatchMetadatas.end()) {
            auto& metadata = result->second;
            metadata.watchCount--;
            if (metadata.watchCount <= 0) {
                g_FileWatcher->removeWatch(watchID);
                g_WatchMetadatas.erase(result);
            }
        }
        return 0;
    }
}

GMOD_MODULE_OPEN() {
    g_pFullFileSystem = InterfacePointers::FileSystem();
    if (!g_pFullFileSystem)
        LUA->ThrowError("failed to get IFileSystem");

    g_Lua = reinterpret_cast<GarrysMod::Lua::ILuaInterface*>(LUA);

    LUA->GetField(GarrysMod::Lua::INDEX_GLOBAL, "hook");
    LUA->GetField(-1, "Add");
    LUA->PushString("Think");
    LUA->PushString("__ESFW_THINK");
    LUA->PushCFunction(LuaFuncs::Think);
    LUA->Call(3, 0);
    LUA->Pop();

    g_FileWatcher = std::make_unique<efsw::FileWatcher>();
    g_UpdateListener = std::make_unique<UpdateListener>();
    g_FileWatcher->watch();

    LUA->CreateTable();
        LUA->PushCFunction(LuaFuncs::Watch); LUA->SetField(-2, "Watch");
        LUA->PushCFunction(LuaFuncs::Unwatch); LUA->SetField(-2, "Unwatch");

        LUA->PushNumber(efsw::Actions::Add); LUA->SetField(-2, "ACTION_ADD");
        LUA->PushNumber(efsw::Actions::Delete); LUA->SetField(-2, "ACTION_DELETE");
        LUA->PushNumber(efsw::Actions::Modified); LUA->SetField(-2, "ACTION_MODIFIED");

        LUA->PushNumber(efsw::Errors::FileNotFound); LUA->SetField(-2, "ERROR_FILE_NOT_FOUND");
        LUA->PushNumber(efsw::Errors::FileNotReadable); LUA->SetField(-2, "ERROR_FILE_NOT_READABLE");
        LUA->PushNumber(efsw::Errors::FileOutOfScope); LUA->SetField(-2, "ERROR_FILE_OUT_OF_SCOPE");
        LUA->PushNumber(efsw::Errors::FileRemote); LUA->SetField(-2, "ERROR_FILE_REMOTE");
        LUA->PushNumber(efsw::Errors::FileRepeated); LUA->SetField(-2, "ERROR_FILE_REPEATED");
        LUA->PushNumber(efsw::Errors::Unspecified); LUA->SetField(-2, "ERROR_UNSPECIFIED");
    LUA->SetField(GarrysMod::Lua::INDEX_GLOBAL, "efsw");

    return 0;
}

GMOD_MODULE_CLOSE() {
    g_FileWatcher.reset();
    g_UpdateListener.reset();
    return 0;
}