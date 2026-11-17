// Apps/UltraToDo/core/ToDoStore.h
// Where UltraToDo keeps its tasks: one folder in the user's application data,
//
//   <data>/UltraToDo/tasks.json                    every task
//   <data>/UltraToDo/attachments/<task id>/<file>  the files attached to one
//
// (<data> is the framework's data folder root: ~/.local/share, %APPDATA%,
// ~/Library/Application Support - UltraCanvasDataFolder.h.) A file attached
// to a task is copied there, as a mail program keeps what was attached to a
// message, so the task still has it after the original moved or was deleted.
//
// The task file is written beside itself and renamed into place, so a write
// that fails leaves the list as it was.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "ToDoTask.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraToDo {

class ToDoStore {
public:
    // `folder` is UTF-8; DefaultFolder() is the user's.
    explicit ToDoStore(std::string folder);
    static std::string DefaultFolder();

    const std::string& Folder() const { return folder_; }

    // Reads the task file. A folder without one is an empty list, not an
    // error. False, with `error`, when the file is there and cannot be read.
    bool Load(std::string& error);
    bool Save(std::string& error) const;

    const std::vector<ToDoTask>& Tasks() const { return tasks_; }
    // The tasks in the list's order at `now` (SortByUrgency).
    std::vector<ToDoTask> Sorted(int64_t now) const;
    const ToDoTask* Find(const std::string& id) const;

    // Adds a task (an empty id: it gets one, and `now` as its creation time)
    // or replaces the one with its id. Returns the id.
    std::string Put(ToDoTask task, int64_t now);
    // Removes a task and the files attached to it.
    bool Remove(const std::string& id);
    bool SetDone(const std::string& id, bool done, int64_t now);
    // Records a reminder or an alarm as given (MarkNoticeGiven).
    bool MarkGiven(const ToDoNotice& notice);

    // Copies the file at `sourcePath` (UTF-8) into the task's folder and adds
    // it to the task. A second file of one name is kept as "name (2).ext".
    bool AddAttachment(const std::string& taskId, const std::string& sourcePath, std::string& error);
    // Takes an attachment off the task and deletes its copy.
    bool RemoveAttachment(const std::string& taskId, const std::string& file);
    // The copy's path (UTF-8).
    std::string AttachmentPath(const std::string& taskId, const std::string& file) const;

private:
    ToDoTask* FindMutable(const std::string& id);

    std::string folder_;
    std::vector<ToDoTask> tasks_;
    int64_t nextId_ = 1;
};

} // namespace UltraToDo
