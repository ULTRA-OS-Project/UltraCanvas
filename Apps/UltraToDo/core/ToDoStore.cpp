// Apps/UltraToDo/core/ToDoStore.cpp
// See ToDoStore.h.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "ToDoStore.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasDataFolder.h"
#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;
using UltraCanvas::JSONValue;
using UltraCanvas::OpenFileUtf8;
using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

namespace UltraToDo {

namespace {
const char* const kDataFolderName = "UltraToDo";
const char* const kTaskFileName = "tasks.json";
const char* const kAttachmentFolder = "attachments";
constexpr int kFormat = 1;

fs::path TaskFile(const std::string& folder) {
    return PathFromUtf8(folder) / kTaskFileName;
}

fs::path AttachmentFolderOf(const std::string& folder, const std::string& taskId) {
    return PathFromUtf8(folder) / kAttachmentFolder / PathFromUtf8(taskId);
}

bool ReadWholeFile(const fs::path& path, std::string& out) {
    std::FILE* file = OpenFileUtf8(PathToUtf8(path), "rb");
    if (!file) return false;
    char buffer[65536];
    size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0) out.append(buffer, got);
    const bool ok = std::ferror(file) == 0;
    std::fclose(file);
    return ok;
}

JSONValue ToJson(const ToDoTask& task) {
    JSONValue o = JSONValue::MakeObject();
    o.Set("id", task.id);
    o.Set("title", task.title);
    o.Set("description", task.description);
    o.Set("due", task.due);
    o.Set("dueHasTime", task.dueHasTime);
    o.Set("estimateMinutes", task.estimateMinutes);
    o.Set("reminderMinutes", task.reminderMinutes);
    o.Set("alarm", task.alarm);
    o.Set("done", task.done);
    o.Set("started", task.started);
    o.Set("created", task.created);
    o.Set("completed", task.completed);
    o.Set("remindedFor", task.remindedFor);
    o.Set("alarmedFor", task.alarmedFor);
    JSONValue files = JSONValue::MakeArray();
    for (const ToDoAttachment& a : task.attachments) {
        JSONValue f = JSONValue::MakeObject();
        f.Set("name", a.name);
        f.Set("file", a.file);
        files.Append(std::move(f));
    }
    o.Set("attachments", std::move(files));
    return o;
}

ToDoTask FromJson(const JSONValue& o) {
    ToDoTask task;
    task.id = o.Get("id").GetString();
    task.title = o.Get("title").GetString();
    task.description = o.Get("description").GetString();
    task.due = o.Get("due").GetInteger();
    task.dueHasTime = o.Get("dueHasTime").GetBoolean();
    task.estimateMinutes = static_cast<int>(o.Get("estimateMinutes").GetInteger());
    task.reminderMinutes = static_cast<int>(o.Get("reminderMinutes").GetInteger(kNoReminder));
    task.alarm = o.Get("alarm").GetBoolean();
    task.done = o.Get("done").GetBoolean();
    task.started = o.Get("started").GetInteger();
    task.created = o.Get("created").GetInteger();
    task.completed = o.Get("completed").GetInteger();
    task.remindedFor = o.Get("remindedFor").GetInteger();
    task.alarmedFor = o.Get("alarmedFor").GetInteger();
    for (const JSONValue& f : o.Get("attachments").GetElements()) {
        ToDoAttachment a;
        a.name = f.Get("name").GetString();
        a.file = f.Get("file").GetString();
        if (!a.file.empty()) task.attachments.push_back(std::move(a));
    }
    return task;
}

// A name for the copy that no file system refuses: the separators and the
// characters Windows reserves become "_".
std::string SafeFileName(const std::string& name) {
    std::string out;
    for (const char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        const bool reserved = u < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
                              c == '"' || c == '<' || c == '>' || c == '|';
        out += reserved ? '_' : c;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    return out.empty() ? std::string("file") : out;
}
} // namespace

ToDoStore::ToDoStore(std::string folder) : folder_(std::move(folder)) {}

std::string ToDoStore::DefaultFolder() {
    fs::path root = UltraCanvas::UltraCanvasDataFolderRoot();
    std::error_code ec;
    if (root.empty()) root = fs::current_path(ec);
    return PathToUtf8(root / kDataFolderName);
}

bool ToDoStore::Load(std::string& error) {
    error.clear();
    tasks_.clear();
    nextId_ = 1;
    const fs::path file = TaskFile(folder_);
    std::error_code ec;
    if (!fs::exists(file, ec)) return true;
    std::string text;
    if (!ReadWholeFile(file, text)) {
        error = "cannot read " + PathToUtf8(file);
        return false;
    }
    UltraCanvas::JSONParseResult result;
    const JSONValue root = UltraCanvas::JSON::Parse(text, &result);
    if (!root.IsObject()) {
        error = PathToUtf8(file) + " is not a task list";
        return false;
    }
    for (const JSONValue& entry : root.Get("tasks").GetElements()) {
        ToDoTask task = FromJson(entry);
        if (!task.id.empty()) tasks_.push_back(std::move(task));
    }
    nextId_ = std::max<int64_t>(1, root.Get("nextId").GetInteger(1));
    return true;
}

bool ToDoStore::Save(std::string& error) const {
    error.clear();
    std::error_code ec;
    fs::create_directories(PathFromUtf8(folder_), ec);
    JSONValue root = JSONValue::MakeObject();
    root.Set("format", kFormat);
    root.Set("nextId", nextId_);
    JSONValue list = JSONValue::MakeArray();
    for (const ToDoTask& task : tasks_) list.Append(ToJson(task));
    root.Set("tasks", std::move(list));
    UltraCanvas::JSONSerializeOptions options;
    options.pretty = true;
    options.indentWidth = 2;
    const std::string text = UltraCanvas::JSON::Serialize(root, options);

    const fs::path file = TaskFile(folder_);
    fs::path fresh = file;
    fresh += ".new";
    std::FILE* out = OpenFileUtf8(PathToUtf8(fresh), "wb");
    if (!out) {
        error = "cannot write " + PathToUtf8(fresh);
        return false;
    }
    const bool written = std::fwrite(text.data(), 1, text.size(), out) == text.size();
    const bool closed = std::fclose(out) == 0;
    if (!written || !closed) {
        fs::remove(fresh, ec);
        error = "cannot write " + PathToUtf8(fresh);
        return false;
    }
    fs::rename(fresh, file, ec);
    if (ec) {
        error = "cannot replace " + PathToUtf8(file) + ": " + ec.message();
        fs::remove(fresh, ec);
        return false;
    }
    return true;
}

std::vector<ToDoTask> ToDoStore::Sorted(int64_t now) const {
    std::vector<ToDoTask> sorted = tasks_;
    SortByUrgency(sorted, now);
    return sorted;
}

const ToDoTask* ToDoStore::Find(const std::string& id) const {
    for (const ToDoTask& task : tasks_)
        if (task.id == id) return &task;
    return nullptr;
}

ToDoTask* ToDoStore::FindMutable(const std::string& id) {
    for (ToDoTask& task : tasks_)
        if (task.id == id) return &task;
    return nullptr;
}

std::string ToDoStore::Put(ToDoTask task, int64_t now) {
    if (task.id.empty()) {
        task.id = "task-" + std::to_string(nextId_++);
        if (task.created == 0) task.created = now;
        tasks_.push_back(std::move(task));
        return tasks_.back().id;
    }
    if (ToDoTask* existing = FindMutable(task.id)) {
        *existing = std::move(task);
        return existing->id;
    }
    tasks_.push_back(std::move(task));
    return tasks_.back().id;
}

bool ToDoStore::Remove(const std::string& id) {
    const auto it = std::find_if(tasks_.begin(), tasks_.end(),
                                 [&id](const ToDoTask& task) { return task.id == id; });
    if (it == tasks_.end()) return false;
    tasks_.erase(it);
    std::error_code ec;
    fs::remove_all(AttachmentFolderOf(folder_, id), ec);
    return true;
}

bool ToDoStore::SetDone(const std::string& id, bool done, int64_t now) {
    ToDoTask* task = FindMutable(id);
    if (!task) return false;
    task->done = done;
    task->completed = done ? now : 0;
    return true;
}

bool ToDoStore::SetStarted(const std::string& id, bool started, int64_t now) {
    ToDoTask* task = FindMutable(id);
    if (!task) return false;
    task->started = started ? now : 0;
    return true;
}

bool ToDoStore::MarkGiven(const ToDoNotice& notice) {
    ToDoTask* task = FindMutable(notice.taskId);
    if (!task) return false;
    MarkNoticeGiven(*task, notice);
    return true;
}

bool ToDoStore::AddAttachment(const std::string& taskId, const std::string& sourcePath,
                              std::string& error) {
    error.clear();
    ToDoTask* task = FindMutable(taskId);
    if (!task) {
        error = "no such task";
        return false;
    }
    const fs::path source = PathFromUtf8(sourcePath);
    std::error_code ec;
    if (!fs::is_regular_file(source, ec)) {
        error = sourcePath + " is not a file";
        return false;
    }
    const fs::path folder = AttachmentFolderOf(folder_, taskId);
    fs::create_directories(folder, ec);
    const std::string name = PathToUtf8(source.filename());
    const fs::path safe = PathFromUtf8(SafeFileName(name));
    const std::string stem = PathToUtf8(safe.stem());
    const std::string extension = PathToUtf8(safe.extension());
    fs::path target = folder / safe;
    for (int n = 2; fs::exists(target, ec); ++n)
        target = folder / PathFromUtf8(stem + " (" + std::to_string(n) + ")" + extension);
    fs::copy_file(source, target, ec);
    if (ec) {
        error = "cannot copy " + sourcePath + ": " + ec.message();
        return false;
    }
    task->attachments.push_back({name, PathToUtf8(target.filename())});
    return true;
}

bool ToDoStore::RemoveAttachment(const std::string& taskId, const std::string& file) {
    ToDoTask* task = FindMutable(taskId);
    if (!task) return false;
    const auto it = std::find_if(task->attachments.begin(), task->attachments.end(),
                                 [&file](const ToDoAttachment& a) { return a.file == file; });
    if (it == task->attachments.end()) return false;
    task->attachments.erase(it);
    std::error_code ec;
    fs::remove(PathFromUtf8(AttachmentPath(taskId, file)), ec);
    return true;
}

std::string ToDoStore::AttachmentPath(const std::string& taskId, const std::string& file) const {
    return PathToUtf8(AttachmentFolderOf(folder_, taskId) / PathFromUtf8(file));
}

} // namespace UltraToDo
