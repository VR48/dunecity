/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <misc/FileSystem.h>
#include <misc/string_util.h>
#include <misc/exceptions.h>
#include <misc/SDL2pp.h>
#include <misc/fnkdat.h>

#include <stdio.h>
#include <algorithm>
#include <ctype.h>

#include <SDL_filesystem.h>

#ifdef _WIN32
#include <io.h>
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif


std::list<std::string> getFileNamesList(const std::string& directory, const std::string& extension, bool IgnoreCase, FileListOrder fileListOrder)
{
    std::list<FileInfo> files = getFileList(directory, extension, IgnoreCase, fileListOrder);

    std::list<std::string> fileNames;

    for(const FileInfo& fileInfo : files) {
        fileNames.push_back(fileInfo.name);
    }

    return fileNames;
}

static bool cmp_Name_Asc(const FileInfo& a, const FileInfo& b) { return (a.name.compare(b.name) < 0); }
static bool cmp_Name_CaseInsensitive_Asc(const FileInfo& a, const FileInfo& b) {
  unsigned int i=0;
  while((i < a.name.length()) && (i < b.name.length())) {
    if(tolower(a.name[i]) < tolower(b.name[i])) {
        return true;
    } else if (tolower(a.name[i]) > tolower(b.name[i])) {
        return false;
    }
    i++;
  }

  return (a.name.length() < b.name.length());
}

static bool cmp_Name_Dsc(const FileInfo& a, const FileInfo& b) { return (a.name.compare(b.name) > 0); }
static bool cmp_Name_CaseInsensitive_Dsc(const FileInfo& a, const FileInfo& b) {
  unsigned int i=0;
  while((i < a.name.length()) && (i < b.name.length())) {
    if(tolower(a.name[i]) < tolower(b.name[i])) {
        return false;
    } else if (tolower(a.name[i]) > tolower(b.name[i])) {
        return true;
    }
    i++;
  }

  return (a.name.length() > b.name.length());
}

static bool cmp_Size_Asc(const FileInfo& a, const FileInfo& b) { return a.size < b.size; }
static bool cmp_Size_Dsc(const FileInfo& a, const FileInfo& b) { return a.size > b.size; }

static bool cmp_ModifyDate_Asc(const FileInfo& a, const FileInfo& b) { return a.modifydate < b.modifydate; }
static bool cmp_ModifyDate_Dsc(const FileInfo& a, const FileInfo& b) { return a.modifydate > b.modifydate; }

std::list<FileInfo> getFileList(const std::string& directory, const std::string& extension, bool bIgnoreCase, FileListOrder fileListOrder)
{
    std::list<FileInfo> Files;
    std::string lowerExtension= bIgnoreCase ? strToLower(extension) : extension;

#ifdef _WIN32
    // on win32 we need an ansi-encoded filepath
    WCHAR szwPath[MAX_PATH];
    char szPath[MAX_PATH];

    if(MultiByteToWideChar(CP_UTF8, 0, directory.c_str(), -1, szwPath, MAX_PATH) == 0) {
        SDL_Log("getFileList(): Conversion of search path from utf-8 to utf-16 failed!");
        return Files;
    }

    if(WideCharToMultiByte(CP_ACP, 0, szwPath, -1, szPath, MAX_PATH, nullptr, nullptr) == 0) {
        SDL_Log("getFileList(): Conversion of search path from utf-16 to ansi failed!");
        return Files;
    }

	intptr_t hFile;

    _finddata_t fdata;

    std::string searchString = std::string(szPath) + "/*";

    if ((hFile = (intptr_t)_findfirst(searchString.c_str(), &fdata)) != -1L) {
        do {
            std::string filename = fdata.name;

            if(filename.length() < lowerExtension.length()+1) {
                continue;
            }

            if(filename[filename.length() - lowerExtension.length() - 1] != '.') {
                continue;
            }

            std::string ext = filename.substr(filename.length() - lowerExtension.length());

            if(bIgnoreCase == true) {
                convertToLower(ext);
            }

            if(ext == lowerExtension) {
                // on win32 we get an ansi-encoded filename
                WCHAR szwFilename[MAX_PATH];
                char szFilename[MAX_PATH];

                if(MultiByteToWideChar(CP_ACP, 0, filename.c_str(), -1, szwFilename, MAX_PATH) == 0) {
                    SDL_Log("getFileList(): Conversion of filename from ansi to utf-16 failed!");
                    continue;
                }

                if(WideCharToMultiByte(CP_UTF8, 0, szwFilename, -1, szFilename, MAX_PATH, nullptr, nullptr) == 0) {
                    SDL_Log("getFileList(): Conversion of search path from utf-16 to utf-8 failed!");
                    continue;
                }

                Files.emplace_back(szFilename, fdata.size, fdata.time_write);
            }
        } while(_findnext(hFile, &fdata) == 0);

        _findclose(hFile);
    }

#else

    DIR * dir = opendir(directory.c_str());
    dirent *curEntry;

    if(dir == nullptr) {
        return Files;
    }

    errno = 0;
    while((curEntry = readdir(dir)) != nullptr) {
            std::string filename = curEntry->d_name;

            if(filename.length() < lowerExtension.length()+1) {
                continue;
            }

            if(filename[filename.length() - lowerExtension.length() - 1] != '.') {
                continue;
            }

            std::string ext = filename.substr(filename.length() - lowerExtension.length());

            if(bIgnoreCase == true) {
                convertToLower(ext);
            }

            if(ext == lowerExtension) {
                std::string fullpath = directory + "/" + filename;
                struct stat fdata;
                if(stat(fullpath.c_str(), &fdata) != 0) {
                    SDL_Log("stat(): %s", strerror(errno));
                    continue;
                }
                Files.push_back(FileInfo(filename, fdata.st_size, fdata.st_mtime));
            }
    }

    if(errno != 0) {
        SDL_Log("readdir(): %s", strerror(errno));
    }

    closedir(dir);

#endif

    switch(fileListOrder) {
        case FileListOrder_Name_Asc: {
            Files.sort(cmp_Name_Asc);
        } break;

        case FileListOrder_Name_CaseInsensitive_Asc: {
            Files.sort(cmp_Name_CaseInsensitive_Asc);
        } break;

        case FileListOrder_Name_Dsc: {
            Files.sort(cmp_Name_Dsc);
        } break;

        case FileListOrder_Name_CaseInsensitive_Dsc: {
            Files.sort(cmp_Name_CaseInsensitive_Dsc);
        } break;

        case FileListOrder_Size_Asc: {
            Files.sort(cmp_Size_Asc);
        } break;

        case FileListOrder_Size_Dsc: {
            Files.sort(cmp_Size_Dsc);
        } break;

        case FileListOrder_ModifyDate_Asc: {
            Files.sort(cmp_ModifyDate_Asc);
        } break;

        case FileListOrder_ModifyDate_Dsc: {
            Files.sort(cmp_ModifyDate_Dsc);
        } break;

        case FileListOrder_Unsorted:
        default: {
            // do nothing
        } break;
    }

    return Files;

}

bool getCaseInsensitiveFilename(std::string& filepath) {

#ifdef _WIN32
    return existsFile(filepath);

#else


    std::string filename;
    std::string path;
    size_t separatorPos = filepath.rfind('/');
    if(separatorPos == std::string::npos) {
        // There is no '/' in the filepath => only filename in current working directory specified
        filename = filepath;
        path = ".";
    } else if(separatorPos == filepath.length()-1) {
        // filepath has an '/' at the end => no filename specified
        return false;
    } else {
        filename = filepath.substr(separatorPos+1, std::string::npos);
        path = filepath.substr(0,separatorPos+1); // path with tailing '/'
    }

    DIR* directory = opendir(path.c_str());
    if(directory == nullptr) {
        return false;
    }

    while(true) {
        errno = 0;
        dirent* directory_entry = readdir(directory);
        if(directory_entry == nullptr) {
            if (errno != 0) {
                closedir(directory);
                return false;
            } else {
                // EOF
                break;
            }
        }

        bool entry_OK = true;
        const char* pEntryName = directory_entry->d_name;
        const char* pFilename = filename.c_str();
        while(true) {
            if((*pEntryName == '\0') && (*pFilename == '\0')) {
                break;
            }

            if(tolower(*pEntryName) != tolower(*pFilename)) {
                entry_OK = false;
                break;
            }
            pEntryName++;
            pFilename++;
        }

        if(entry_OK == true) {
            if(path == ".") {
                filepath = directory_entry->d_name;
            } else {
                filepath = path + directory_entry->d_name;
            }
            closedir(directory);
            return true;
        }
    }
    closedir(directory);
    return false;

#endif

}


bool existsFile(const std::string& path) {
    // try opening the file
    auto RWopsFile = sdl2::RWops_ptr{ SDL_RWFromFile(path.c_str(),"r") };

    if(!RWopsFile) {
        return false;
    };

    return true;
}

std::string readCompleteFile(const std::string& filename) {
    auto RWopsFile = sdl2::RWops_ptr{ SDL_RWFromFile(filename.c_str(),"r") };

    if(!RWopsFile) {
        return "";
    }

    const Sint64 filesize = SDL_RWsize(RWopsFile.get());
    if(filesize < 0) {
        return "";
    }

    std::unique_ptr<char[]> filedata = std::make_unique<char[]>((size_t) filesize);

    if(SDL_RWread(RWopsFile.get(), filedata.get(), (size_t) filesize, 1) != 1) {
        return "";
    }

    std::string retValue(filedata.get(), (size_t) filesize);

    return retValue;
}

bool writeCompleteFile(const std::string& filename, const std::string& content) {
    auto RWopsFile = sdl2::RWops_ptr{ SDL_RWFromFile(filename.c_str(),"wb") };

    if(!RWopsFile) {
        SDL_Log("writeCompleteFile: Failed to open file '%s' for writing: %s", filename.c_str(), SDL_GetError());
        return false;
    }

    if(content.empty()) {
        // Empty file is still valid
        return true;
    }

    size_t bytesWritten = SDL_RWwrite(RWopsFile.get(), content.c_str(), 1, content.size());
    if(bytesWritten != content.size()) {
        SDL_Log("writeCompleteFile: Failed to write complete file '%s': %s", filename.c_str(), SDL_GetError());
        return false;
    }

    return true;
}

std::string getBasename(const std::string& filepath, bool bStripExtension) {

    if(filepath == "/") {
        // special case
        return "/";
    }

    // strip trailing slashes
    const size_t nameEndPos = filepath.find_last_not_of("/\\");

    size_t nameStart = filepath.find_last_of("/\\", nameEndPos);
    if(nameStart == std::string::npos) {
        nameStart = 0;
    } else {
        nameStart++;
    }

    size_t extensionStart;
    if(bStripExtension) {
        extensionStart = filepath.find_last_of('.');
        if(extensionStart == std::string::npos) {
            extensionStart = filepath.length();
        }
    } else {
        extensionStart = (nameEndPos == std::string::npos) ? filepath.length() : nameEndPos+1;
    }

    return filepath.substr(nameStart, extensionStart-nameStart);
}

std::string getDirname(const std::string& filepath) {

    if(filepath == "/") {
        // special case
        return "/";
    }

    // strip trailing slashes
    const size_t nameEndPos = filepath.find_last_not_of("/\\");

    // strip trailing name
    const size_t nameStartPos = filepath.find_last_of("/\\", nameEndPos);

    if(nameStartPos == std::string::npos) {
        return ".";
    }

    // strip separator between dir name and file name
    const size_t dirEndPos = filepath.find_last_not_of("/\\", nameStartPos);

    return filepath.substr(0, dirEndPos+1);
}

bool createDir(const std::string& path) {
    if(path.empty()) {
        return false;
    }

#ifdef _WIN32
    // Convert UTF-8 to wide string for Windows
    WCHAR wszPath[MAX_PATH];
    if(MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wszPath, MAX_PATH) == 0) {
        SDL_Log("createDir: Failed to convert path to wide string");
        return false;
    }
    
    // Try to create the directory
    if(CreateDirectoryW(wszPath, nullptr)) {
        return true;
    }
    
    DWORD error = GetLastError();
    if(error == ERROR_ALREADY_EXISTS) {
        return true;  // Directory already exists
    }
    
    if(error == ERROR_PATH_NOT_FOUND) {
        // Need to create parent directories
        std::string parentPath = getDirname(path);
        if(!parentPath.empty() && parentPath != path) {
            if(createDir(parentPath)) {
                // Now try again
                if(CreateDirectoryW(wszPath, nullptr)) {
                    return true;
                }
                if(GetLastError() == ERROR_ALREADY_EXISTS) {
                    return true;
                }
            }
        }
    }
    
    SDL_Log("createDir: Failed to create directory '%s': error %lu", path.c_str(), error);
    return false;
#else
    // POSIX - try mkdir, recursively create parents if needed
    if(mkdir(path.c_str(), 0755) == 0) {
        return true;
    }
    
    if(errno == EEXIST) {
        // Check if it's actually a directory
        struct stat st;
        if(stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            return true;
        }
        return false;  // Exists but not a directory
    }
    
    if(errno == ENOENT) {
        // Parent doesn't exist, create it
        std::string parentPath = getDirname(path);
        if(!parentPath.empty() && parentPath != path && parentPath != ".") {
            if(createDir(parentPath)) {
                // Try again
                if(mkdir(path.c_str(), 0755) == 0) {
                    return true;
                }
                if(errno == EEXIST) {
                    return true;
                }
            }
        }
    }
    
    SDL_Log("createDir: Failed to create directory '%s': %s", path.c_str(), strerror(errno));
    return false;
#endif
}

bool copyFile(const std::string& src, const std::string& dst) {
    // Read source file
    std::string content = readCompleteFile(src);
    if(content.empty() && !existsFile(src)) {
        SDL_Log("copyFile: Source file '%s' does not exist or is empty", src.c_str());
        return false;
    }
    
    // Write to destination
    return writeCompleteFile(dst, content);
}

bool deleteFile(const std::string& path) {
    if(remove(path.c_str()) == 0) {
        return true;
    }
    
    if(errno == ENOENT) {
        return true;  // Already doesn't exist
    }
    
    SDL_Log("deleteFile: Failed to delete '%s': %s", path.c_str(), strerror(errno));
    return false;
}

std::list<std::string> getDirectoryList(const std::string& directory) {
    std::list<std::string> dirs;
    
    SDL_Log("getDirectoryList: scanning '%s'", directory.c_str());

#ifdef _WIN32
    WCHAR wszPath[MAX_PATH];
    if(MultiByteToWideChar(CP_UTF8, 0, directory.c_str(), -1, wszPath, MAX_PATH) == 0) {
        SDL_Log("getDirectoryList: Conversion from utf-8 to utf-16 failed!");
        return dirs;
    }

    char szPath[MAX_PATH];
    if(WideCharToMultiByte(CP_ACP, 0, wszPath, -1, szPath, MAX_PATH, nullptr, nullptr) == 0) {
        SDL_Log("getDirectoryList: Conversion from utf-16 to ansi failed!");
        return dirs;
    }

    _finddata_t fdata;
    std::string searchString = std::string(szPath) + "/*";
    intptr_t hFile = (intptr_t)_findfirst(searchString.c_str(), &fdata);
    
    if(hFile != -1L) {
        do {
            if(fdata.attrib & _A_SUBDIR) {
                std::string name = fdata.name;
                if(name != "." && name != "..") {
                    // Convert ansi to utf-8
                    WCHAR wszName[MAX_PATH];
                    char szName[MAX_PATH];
                    if(MultiByteToWideChar(CP_ACP, 0, name.c_str(), -1, wszName, MAX_PATH) != 0 &&
                       WideCharToMultiByte(CP_UTF8, 0, wszName, -1, szName, MAX_PATH, nullptr, nullptr) != 0) {
                        dirs.push_back(szName);
                    }
                }
            }
        } while(_findnext(hFile, &fdata) == 0);
        _findclose(hFile);
    }
#else
    DIR* dir = opendir(directory.c_str());
    if(dir == nullptr) {
        return dirs;
    }

    dirent* entry;
    while((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if(name == "." || name == "..") {
            continue;
        }
        
        // Check if it's a directory
        std::string fullPath = directory + "/" + name;
        struct stat st;
        if(stat(fullPath.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            dirs.push_back(name);
        }
    }
    closedir(dir);
#endif

    SDL_Log("getDirectoryList: found %zu directories", dirs.size());
    return dirs;
}

static std::string duneLegacyDataDir;

std::string getDuneLegacyDataDir() {
    if(duneLegacyDataDir.empty()) {

        std::string dataDir;
#ifdef __ANDROID__
        char androidDataPath[FILENAME_MAX];
        if(fnkdat(nullptr, androidDataPath, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT) == 0
           && androidDataPath[0] != '\0') {
            dataDir = androidDataPath;
            SDL_Log("Using Android app storage as data dir: %s", dataDir.c_str());
        }
#endif

#ifdef DUNELEGACY_DATADIR
        // Only use the compile-time install path if it actually exists
        // (i.e. the binary was installed, not run from a build directory)
        if(dataDir.empty()) {
            struct stat dirCheck;
            if (stat(DUNELEGACY_DATADIR, &dirCheck) == 0 && S_ISDIR(dirCheck.st_mode)) {
                dataDir = DUNELEGACY_DATADIR;
            } else {
                SDL_Log("DUNELEGACY_DATADIR '%s' not found, falling through to SDL_GetBasePath()", DUNELEGACY_DATADIR);
            }
        }
#endif

        if((dataDir.empty()) || (dataDir == ".") || (dataDir == "./") || (dataDir == ".\\")) {
            char* basePath = SDL_GetBasePath();
            if(basePath == nullptr) {
                THROW(sdl_error, "SDL_GetBasePath() failed: %s!", SDL_GetError());
            }
            dataDir = std::string(basePath);
            SDL_free(basePath);
            
#ifdef __APPLE__
            // On macOS app bundles, SDL_GetBasePath() returns Contents/MacOS/
            // but data files are in Contents/Resources/
            // Check if we're in an app bundle and adjust path
            if(dataDir.find(".app/Contents/MacOS") != std::string::npos) {
                // Remove trailing slash if present
                if(!dataDir.empty() && dataDir.back() == '/') {
                    dataDir.pop_back();
                }
                // Go up from MacOS and into Resources
                size_t pos = dataDir.rfind("/MacOS");
                if(pos != std::string::npos) {
                    dataDir = dataDir.substr(0, pos) + "/Resources/";
                    SDL_Log("Adjusted data dir for macOS app bundle: %s", dataDir.c_str());
                }
            }
#endif
        }

        duneLegacyDataDir = dataDir;
    }
    return duneLegacyDataDir;
}
