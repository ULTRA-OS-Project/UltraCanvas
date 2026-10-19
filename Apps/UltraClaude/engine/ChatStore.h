// Apps/UltraClaude/engine/ChatStore.h
// The chats UltraClaude remembers, so the window can list them and pick one
// up again. Each chat is one Claude Code conversation: its CLI session id
// (what `--resume` continues), the folder it runs in - the CLI keeps its
// sessions per folder, so a chat always goes back to its own - and the model
// and permission mode it was last sent with.
//
// On disk, in UltraCanvasSettingsFolder()/UltraClaude:
//   chats.json              {"version": 1, "chats": [ {...}, ... ]}
//   transcripts/<id>.md     what the transcript showed, as markdown
// The transcript is UltraClaude's own copy, so a chat reopens exactly as it
// looked; the CLI's session files are its internal format and not read.
// Both files are written to a temporary name and renamed into place, so a
// crash mid-write leaves the previous version, never half a file.
//
// No UI and no process in here: the window and the --list-chats mode share it.
//
// Version: 0.1.0
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace UltraClaude {

struct ChatRecord {
    std::string id;              // UltraClaude's own, stable for the chat's life
    std::string sessionId;       // the CLI's, to resume; empty before the first reply
    std::string title;           // the first prompt's first line, shortened
    std::string folder;          // where the CLI runs (UTF-8)
    std::string model;           // dropdown value ("" = the CLI's default)
    std::string permissionMode;  // dropdown value ("" = the CLI's default)
    int64_t createdAt = 0;       // seconds since the Unix epoch
    int64_t updatedAt = 0;       // last turn; the list is sorted by this, newest first
};

class ChatStore {
public:
    // folder: where chats.json and transcripts/ live (need not exist yet).
    explicit ChatStore(std::filesystem::path folder);

    // UltraCanvasSettingsFolder()/UltraClaude, or empty when there is no home.
    static std::filesystem::path DefaultFolder();

    // Reads chats.json. A missing file is an empty list, not an error; a
    // damaged one is reported and leaves the list empty (the file is kept).
    bool Load(std::string& outError);
    // Writes chats.json.
    bool Save(std::string& outError) const;

    // Newest first.
    const std::vector<ChatRecord>& Chats() const { return chats_; }
    ChatRecord* Find(const std::string& id);

    // A new chat at the top of the list, not saved yet.
    ChatRecord& Create(const std::string& title, const std::string& folder,
                       const std::string& model, const std::string& permissionMode);
    // Marks the chat used now and moves it to the top.
    void Touch(const std::string& id);
    // Forgets the chat and deletes its transcript.
    void Remove(const std::string& id);

    std::string LoadTranscript(const std::string& id) const;
    bool SaveTranscript(const std::string& id, const std::string& markdown,
                        std::string& outError) const;

    // A chat's title from its first prompt: the first non-empty line, cut to
    // maxBytes on a UTF-8 boundary.
    static std::string TitleFromPrompt(const std::string& prompt, size_t maxBytes = 60);

private:
    std::filesystem::path TranscriptPath(const std::string& id) const;
    void SortNewestFirst();

    std::filesystem::path folder_;
    std::vector<ChatRecord> chats_;
};

} // namespace UltraClaude
