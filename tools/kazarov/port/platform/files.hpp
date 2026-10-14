#pragma once

#include <cstddef>
#include <filesystem>

enum class FilesResult {
    kWritten,
    kExists, // Not written: the path was taken and replacing was not allowed.
    kFailed,
};

// Writes data to path so that it is on the disk once kWritten returns: a temporary file of its own
// beside path, created exclusively, written, flushed and closed, then moved onto path with the
// system's write-through or followed by a sync of the directory. Before that move path keeps what
// it held. Without replace, a path that exists gives kExists. Removing the temporary file is best
// effort: one named <path>.<hex>.tmp is left when the process dies during the call or the removal
// fails, and can be deleted by hand.
FilesResult FilesWrite(const std::filesystem::path &path, const void *data, std::size_t size, bool replace);

// Reads up to size bytes of the regular file at path and returns how many; anything else at path,
// a directory or a FIFO, reads as nothing, without waiting on it.
std::size_t FilesRead(const std::filesystem::path &path, void *data, std::size_t size);

// Creates a directory at path, where nothing may be: kExists when anything is. Publishes with a
// write-through move on Windows; on POSIX syncs the parent. A failed sync may leave the new name.
FilesResult FilesCreateDirectory(const std::filesystem::path &path);
