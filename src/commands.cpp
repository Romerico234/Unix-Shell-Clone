#include "commands.h"
#include <limits>
#include <string>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <iostream>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <ctime>   
#include <regex>
#include <utime.h>
#include <ctime>

/**
 * @brief Display a list of all supported shell commands
 * @param args Must be empty
 * @return Status code and a help message on success
 */
CommandResult Commands::helpCommand(const std::vector<std::string>& args) {
    if (!args.empty()) {
        return {1, "", "help: this command takes no arguments"};
    }

    std::string out =
        "Available Commands:\n"
        "  cd [dir]                                 Change directory.\n"
        "  clr                                      Clear the screen.\n"
        "  dir [-a] [-A] [-l] [path]                List directory contents.\n"
        "  environ                                  Display environment variables.\n"
        "  echo [text]                              Print text.\n"
        "  help                                     Show help.\n"
        "  pause                                    Pause shell.\n"
        "  quit                                     Exit shell.\n"
        "  chmod <mode> <file>                      Change permissions.\n"
        "  chown <owner> <file>                     Change ownership.\n"
        "  ls [-a] [-A] [-l] [path]                 List directory contents.\n"
        "  pwd                                      Print working directory.\n"
        "  cat <file>...                            Print file contents.\n"
        "  mkdir <dir>                              Create directory.\n"
        "  rmdir [-p] <dir>                         Remove directory.\n"
        "  rm [-r] <path>                           Remove file or directory.\n"
        "  cp <src>... <dst>                        Copy.\n"
        "  mv <src> <dst>                           Move.\n"
        "  touch <file>                             Create empty file.\n"
        "  grep [OPTIONS] <pattern> <file>          Search text.\n"
        "  wc [-l] [-w] [-c]                        Count lines/words/chars.";

    return {0, out, ""};
}

/**
 * @brief Print all provided arguments separated by single spaces
 * @param args A vector of strings to print
 * @return Status code and the formatted output text
 */
CommandResult Commands::echoCommand(const std::vector<std::string>& args) {
    std::string out;

    for (const std::string& arg : args) {
        out += arg + " ";
    }

    return {0, out, ""};
}

/**
 * @brief Pause execution of process
 * @param args Must be empty
 * @return Status code, empty output on success or error message on failure
 */
CommandResult Commands::pauseCommand(const std::vector<std::string> &args)
{
    if (!args.empty()){
        return {1, "", "pause: this command takes no arguments"};
    }

    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    return {0, "", ""};
}

/**
 * @brief List the contents of files and directories
 * @param args Optional flags or file/directory paths:
 *        - "-a" include hidden entries
 *        - "-A" exclude "." and ".."
 *        - "-l" include detailed file information
 *        - otherwise, treat as file/directory operand
 * @return Status code, output, and possible error messages
 */
CommandResult Commands::lsCommand(const std::vector<std::string>& args) {
    bool showAll = false;
    bool almostAll = false;
    bool longList = false;

    std::vector<std::string> paths;
    std::string out;

    for (const std::string& arg : args) {
        if (arg == "-a") showAll = true;
        else if (arg == "-A") almostAll = true;
        else if (arg == "-l") longList = true;
        else if (arg.size() > 1 && arg[0] == '-') {
            return {1, "", "ls: invalid flag -- '" + arg + "'"};
        } else {
            paths.push_back(arg);
        }
    }

    if (paths.empty()) {
        paths.push_back(".");
    }

    for (const std::string& p : paths) {
        struct stat info;
        if (stat(p.c_str(), &info) == -1) {
            return {1, "", "ls: cannot access '" + p + "': " + std::string(strerror(errno))};
        }
        
        // If path is a file
        if (!S_ISDIR(info.st_mode)) {
            if (longList) {
                out += formatLsLongListing(p, info);
            } else {
                out += p + "\n";
            } 
            continue;
        }

        if (paths.size() > 1) {
            out += p + ":\n";
        }

        DIR* dirp = opendir(p.c_str());
        if (!dirp) {
            return {1, "", "ls: cannot open directory '" + p + "': " + std::string(strerror(errno))};
        }

        struct dirent* dp;
        while ((dp = readdir(dirp)) != nullptr) {
            std::string name = dp->d_name;

            if (!showAll && !almostAll && name[0] == '.') {
                continue;
            }
            
            if (almostAll && (name == "." || name == "..")) {
                continue;
            }

            std::string full = p + "/" + name;

            if (longList) {
                struct stat finfo;
                if (stat(full.c_str(), &finfo) == -1) {
                    closedir(dirp);
                    return {1, "", "ls: cannot access '" + name + "': " + std::string(strerror(errno))};
                }
                out += formatLsLongListing(name, finfo);
            } else {
                out += name + " ";
            }
        }

        closedir(dirp);
    }

    return {0, stripTrailingNewline(out), ""};
}

/**
 * @brief Alias for ls command
 * @param args Refer to ls command
 * @return Status code, directory listing output, and possible error messages
 */
CommandResult Commands::dirCommand(const std::vector<std::string>& args) {
    return lsCommand(args);
}

/**
 * @brief Change the current working directory
 * @param args
 *        - zero arguments: go to the home directory
 *        - one argument: change to the specified path (supports ~ expansion)
 * @return Status code, empty output on success or error message on failure
 */
CommandResult Commands::cdCommand(const std::vector<std::string>& args) {
    const char* home = getenv("HOME");

    if (args.empty()){
        if(chdir(home) == -1) {
            return {1, "", "cd: failed to change directory"};
        };
    } else if (args.size() == 1) {
        std::string path = args[0];

        if (path[0] == '~') {
            path = std::string(home) + path.substr(1);
        } 

        if(chdir(path.c_str()) == -1) {
            std::string errorMsg = "cd: failed to change directory: " + path;
            return {1, "", errorMsg};
        };

    } else {
        return {1, "", "cd: too many arguments"};
    }

    return {0, "", ""};
}

/**
 * @brief Deletes the specified directory
 * @param args
 *       - one argument: path to the directory to remove
 *       - two arguments: "-p" flag followed by the specified paths to remove
 * * @return Status code, directory deletion messages on success, or an error message on failure
 */
CommandResult Commands::rmdirCommand(const std::vector<std::string>& args) {
    if (args.empty()) {
        return {1, "", "rmdir: missing operand"};
    }

    if (args.size() == 2) {
        if (args[0] == "-p") {
            std::string path = args[1];
            std::string out = "";

            if (path.empty()) {
                return {1, "", "rmdir: no path specified"};
            }

            // Avoids possible infinite loops (because of trailing slashes)
            while (!path.empty() && path.back() == '/') {
                path.pop_back();
            }

            while (!path.empty()) {

                if (rmdir(path.c_str()) == -1) {
                    return {1, "", formatRmdirErrorMsg(path)};
                }

                size_t pos = path.find_last_of('/');
                if (pos == std::string::npos) break;

                path = path.substr(0, pos);

                // Avoids deleting the root directory
                if (path == "" || path == "/") break;
            }

            return {0, "", ""};
        }
        else {
            return {1, "", "rmdir: unrecognized option '" + args[0] + "'"};
        }
    }

    if (args.size() > 2) {
        return {1, "", "rmdir: too many arguments"};
    }

    std::string path = args[0];

    if (rmdir(path.c_str()) == -1) {
        return {1, "", formatRmdirErrorMsg(path)};
    }

    return {0, "", ""};
}

/**
 * @brief Creates a file if it does not exit or updates an existing file's access/modification time
 * @param args The file name
 * @return Status code, empty output on success or error message on failure
 */
CommandResult Commands::touchCommand(const std::vector<std::string>& args) {
    if (args.empty() || args.size() > 1) {
        return {1, "", "touch: invalid arguments passed"};
    }

    std::string fileName = args[0];

    struct stat st;
    bool exists = (stat(fileName.c_str(), &st) == 0);

    if (!exists) {
        int fd = open(fileName.c_str(), O_CREAT | O_WRONLY, 0644);
        if (fd == -1) {
            return {1, "", "touch: cannot create file '" + fileName + "': " + strerror(errno)};
        }
        close(fd);
        return {0, "", ""};
    }

    struct utimbuf newTimes;
    newTimes.actime  = time(nullptr);  
    newTimes.modtime = time(nullptr);  

    if (utime(fileName.c_str(), &newTimes) == -1) {
        return {1, "", "touch: failed to update timestamps for '" + fileName + "': " + strerror(errno)};
    }

    return {0, "", ""};
}

/**
 * @brief Copy a file or directory to a destination path.
 * @param args Source and destination paths
 * @return Status code, empty output on success or an error message on failure
 */
CommandResult Commands::cpCommand(const std::vector<std::string>& args) {
    /**
     * TODO: Implement "-r" flag
    */

    if (args.empty()) {
        return {1, "", "cp: missing operand"};
    }

    if (args.size() == 1) {
        return {1, "", "cp: missing destination file operand after '" + args[0] + "'"};
    }

    std::string dest = args.back();

    struct stat stDest;
    bool destIsDir = stat(dest.c_str(), &stDest) == 0 && S_ISDIR(stDest.st_mode);

    // If multiple sources, dest MUST be a directory
    int numSources = args.size() - 1;
    if (numSources > 1 && !destIsDir) {
        return {1, "", "cp: target '" + dest + "' is not a directory"};
    }

    for (int i = 0; i < numSources; ++i) {
        std::string src = args[i];

        // Rejecting directory sources because no -r support yet
        struct stat stSrc;
        if (stat(src.c_str(), &stSrc) == 0 && S_ISDIR(stSrc.st_mode)) {
            return {1, "", "cp: omitting directory '" + src + "'"};
        }

        std::string finalDest = dest;
        if (destIsDir) {
            ssize_t pos = src.find_last_of('/'); 
            std::string filename = (pos == std::string::npos) ? src : src.substr(pos + 1);
            finalDest = dest + "/" + filename;
        }

        int fdSrc = open(src.c_str(), O_RDONLY);
        if (fdSrc == -1) {
            return {1, "", "cp: cannot open source file '" + src + "': " + std::string(strerror(errno))};
        }

        int fdDest = open(finalDest.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fdDest == -1) {
            close(fdSrc);
            return {1, "", "cp: cannot create destination file '" + finalDest + "': " + std::string(strerror(errno))};
        }

        char buffer[1024];
        ssize_t bytesRead;
        while ((bytesRead = read(fdSrc, buffer, sizeof(buffer))) > 0) {
            if (write(fdDest, buffer, bytesRead) != bytesRead) {
                close(fdSrc);
                close(fdDest);
                return {1, "", "cp: write error on '" + finalDest + "': " + std::string(strerror(errno))};
            }
        }

        if (bytesRead == -1) {
            close(fdSrc);
            close(fdDest);
            return {1, "", "cp: read error on '" + src + "': " + std::string(strerror(errno))};
        }

        close(fdSrc);
        close(fdDest);
    }

    return {0, "", ""};
}

/**
 * @brief Change the owner of a file.
 * @param args Username and file path
 * @return Status code, empty output on success or an error message on failure
 */
CommandResult Commands::chownCommand(const std::vector<std::string>& args) {
    if (args.empty()) {
        return {1, "", "chown: missing arguments"};    
    }

    if (args.size() < 2) {
        return {1, "", "chown: missing operand"};    
    }

    std::string username = args[0];
    std::string file = args[1];

    struct passwd* pw = getpwnam(username.c_str());
    if (!pw) {
        return {1, "", "chown: no such user found"};
    }
    uid_t newOwner = pw->pw_uid;

    for (int i = 1; i < args.size(); ++i) {
        const std::string& file = args[i];

        struct stat st;
        if (stat(file.c_str(), &st) == -1) {
            return {1, "", "chown: cannot access '" + file + "': " + std::string(strerror(errno))};
        }

        if (chown(file.c_str(), newOwner, -1) == -1) {
            return {1, "", "chown: failed to change owner of '" + file + "': " + std::string(strerror(errno))};
        }
    }

    return {0, "", ""};
}

/**
 * @brief Search for a pattern in one or more files using regex
 * @param args The (regex) pattern, the file(s) to search in, and optional flags:
 *        - "-i"  Perform case-insensitive matching
 *        - "-n"  Prefix each matching line with its line number
 *        - "-v"  Select non-matching lines
 *        - "-w"  Match whole words only
 *        - "-c"  Print only the count of matching lines
 *        - "-o"  Print only the matching substring(s) instead of entire lines
 *        - "-m <num>"  Stop after <num> matches
 * @return Status code and matched output text, or an error message on failure
 */
CommandResult Commands::grepCommand(const std::vector<std::string>& args) {
    /**
     * TODO: Implement additional flags and support flag combinations
     */

    if (args.size() < 2) {
        return {1, "", "grep: missing arguments"};
    }

    bool opt_i = false;
    bool opt_n = false;
    bool opt_v = false;
    bool opt_w = false;
    bool opt_c = false;
    bool opt_o = false;
    int  opt_m = -1;

    int idx = 0;
    int flagCount = 0;

    while (idx < args.size() && args[idx][0] == '-') {
        if (++flagCount > 1) {
            return {1, "", "grep: only one flag can be used at a time"};
        }

        const std::string& flag = args[idx];

        if (flag == "-i") opt_i = true;
        else if (flag == "-n") opt_n = true;
        else if (flag == "-v") opt_v = true;
        else if (flag == "-w") opt_w = true;
        else if (flag == "-c") opt_c = true;
        else if (flag == "-o") opt_o = true;
        else if (flag == "-m") {
            if (idx + 1 >= args.size())
                return {1, "", "grep: missing argument for -m"};
            opt_m = std::stoi(args[++idx]);
        }
        else break;

        ++idx;
    }

    if (idx >= args.size()) {
        return {1, "", "grep: missing pattern"};
    }

    std::string pattern = args[idx++];

    if (idx >= args.size()) {
        return {1, "", "grep: missing file operand"};
    }
    
    std::regex_constants::syntax_option_type flags;
    if (opt_i) {
        flags = std::regex_constants::ECMAScript | std::regex_constants::icase;
    } else {
        flags = std::regex_constants::ECMAScript;
    }

    if (opt_w) pattern = "\\b" + pattern + "\\b";

    std::regex re;
    try {
        re = std::regex(pattern, flags);
    } catch (...) {
        return {1, "", "grep: invalid regex"};
    }

    bool multipleFiles = (args.size() - idx) > 1;

    int totalMatches = 0;
    std::string out;

    for (int i = idx; i < args.size(); ++i) {

        const std::string& file = args[i];

        int fd = open(file.c_str(), O_RDONLY);
        if (fd == -1) {
            return {1, "", "grep: cannot open file '" + file + "'"};
        }

        char buffer[1024];
        ssize_t bytes;
        std::string lineBuffer;
        int lineNumber = 1;

        while ((bytes = read(fd, buffer, sizeof(buffer))) > 0) {
            lineBuffer.append(buffer, bytes);

            size_t newlinePos;
            while ((newlinePos = lineBuffer.find('\n')) != std::string::npos) {
                std::string line = lineBuffer.substr(0, newlinePos);
                lineBuffer.erase(0, newlinePos + 1);

                std::string matchedText;
                bool matched = matchesPattern(line, re, opt_o, matchedText);

                if (opt_v) {
                    matched = !matched;
                }

                if (matched) {
                    ++totalMatches;

                    if (opt_m != -1 && totalMatches > opt_m) {
                        close(fd);
                        if (opt_c){
                            return {0, std::to_string(totalMatches), ""};
                        }

                        if (!out.empty() && out.back() == '\n') {
                            out.pop_back();
                        }

                        return {0, out, ""};
                    }

                    if (!opt_c) {
                        if (multipleFiles) {
                            out += file + ":";
                        }

                        if (opt_n) {
                            out += std::to_string(lineNumber) + ":";
                        }

                        if (opt_v && !opt_o) {
                            out += line + "\n";\
                        } else {
                            out += matchedText + "\n";
                        }
                    }
                }

                ++lineNumber;
            }
        }

        // Last line if it's not newline
        if (!lineBuffer.empty()) {
            std::string matchedText;
            bool matched = matchesPattern(lineBuffer, re, opt_o, matchedText);

            if (opt_v) {
                matched = !matched;
            }

            if (matched) {
                ++totalMatches;
                if (opt_m != -1 && totalMatches > opt_m) {
                    close(fd);
                    if (opt_c) {
                        return {0, std::to_string(totalMatches), ""};
                    }

                    if (!out.empty() && out.back() == '\n') {
                        out.pop_back();
                    }

                    return {0, out, ""};
                }

                if (!opt_c) {
                    if (multipleFiles) {
                        out += file + ":";
                    }

                    if (opt_n) {
                        out += std::to_string(lineNumber) + ":";
                    }

                    if (opt_v && !opt_o) {
                        out += lineBuffer + "\n";
                    } else {
                        out += matchedText + "\n";
                    }
                }
            }
        }

        close(fd);
    }

    if (opt_c) {
        return {0, std::to_string(totalMatches), ""};
    }

    if (totalMatches == 0) {
        return {1, "", ""};
    }

    return {0, stripTrailingNewline(out), ""};
}


/**
 * @brief Exit the shell. Terminates the shell program immediately.
 * @param args Must be empty
 * @return Status code indicating shell termination.
 */
CommandResult Commands::quitCommand(const std::vector<std::string>& args) {
    if (!args.empty()) {
        return {1, "", "quit: this command takes no arguments"};
    }

    std::cout << "[Shell Terminated]\n";
    std::exit(0);
}

/**
 * @brief Clears all text from the terminal window using ANSI escape codes.
 * @param args Must be empty
 * @return Status code, clears shell on success, error message on failure
 */
CommandResult Commands::clrCommand(const std::vector<std::string>& args) {
    if (!args.empty()) {
        return {1, "", "clr: takes no arguments"};
    }

    // __NO_NL__ is a marker to notify the shell NOT to print out a newline
    return {0, "__NO_NL__\e[H\e[J", ""};
}

/**
 * @brief Displays the path of the directory the shell is currently in.
 * @param args Must be empty
 * @return Status code and the current directory path
 */
CommandResult Commands::pwdCommand(const std::vector<std::string>& args) {
    if (!args.empty()) {
        return {1, "", "pwd: this command takes no arguments"};
    }

    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        return {1, "", "pwd: failed to get current directory"};
    }

    return {0, std::string(cwd), ""};
}

/**
 * @brief Display all environment variables
 * @param args Must be empty
 * @return Status code and printed file contents.
 */
CommandResult Commands::environCommand(const std::vector<std::string>& args) {
    if (!args.empty()) {
        return {1, "", "environ: this command takes no arguments"};
    }

    std::string out;

    for (char **env = environ; *env != nullptr; ++env) {
        out += std::string(*env) + "\n";
    }

    return {0, stripTrailingNewline(out), ""};
}

/**
 * @brief Reads and prints the contents of each file provided in order.
 * @param args List of file paths to print
 * @return Status code, printed file contents on success, or error message on failure
 */
CommandResult Commands::catCommand(const std::vector<std::string>& args) {
    if (args.empty()) {
        return {1, "", "cat: missing file operand"};
    }

    std::string out;
    const size_t bufferSize = 4096; 
    char buffer[bufferSize];

    for (const std::string& filename : args) {
        int fd = open(filename.c_str(), O_RDONLY); 
        if (fd == -1) {
            return {1, "", "cat: cannot open " + filename + ": " + strerror(errno)};
        }

        ssize_t bytesRead;
        while ((bytesRead = read(fd, buffer, bufferSize)) > 0) { 
            out.append(buffer, bytesRead);  
        }

        if (bytesRead == -1) {
            return {1, "", "cat: error reading " + filename + ": " + strerror(errno)};
        }

        out.append("\n");  

        close(fd);
    }

    return {0, stripTrailingNewline(out), ""};
}

/**  
 * @brief Count number of lines, words, and characters in a file.
 * @param args List containing exactly one file path and optional files:
 *        "-l" Count lines
 *        "-w" Count words
 *        "-c" Count characters
 * @return Status code, resulting counts on success, error message on failure.
 */
CommandResult Commands::wcCommand(const std::vector<std::string>& args) {
    bool countLines = false;
    bool countWords = false;
    bool countChars = false;
    std::vector<std::string> files;

    for (const std::string& arg : args) {
        if (arg == "-l") countLines = true;
        else if (arg == "-w") countWords = true;
        else if (arg == "-c") countChars = true;
        else files.push_back(arg);
    }

    if (!countLines && !countWords && !countChars) {
        countLines = countWords = countChars = true;
    }

    if (files.empty()) {
        return {1, "", "wc: missing file operand"};
    }

    std::string out;

    for (const std::string& filename : files) {

        if (isFileEmpty(filename)) {
            std::string zeroCount = "";
            if (countLines) zeroCount += "0 ";
            if (countWords) zeroCount += "0 ";
            if (countChars) zeroCount += "0 ";
            zeroCount += filename + "\n";
            out += zeroCount;
            continue;
        }

        int fd = open(filename.c_str(), O_RDONLY);
        if (fd == -1) {
            return {1, "", "wc: cannot open file '" + filename + "': " + strerror(errno)};
        }

        size_t lines = 0, words = 0, chars = 0;
        char buffer[4096];
        bool inWord = false;
        ssize_t bytesRead;
        bool lastCharWasNewline = true;

        while ((bytesRead = read(fd, buffer, sizeof(buffer))) > 0) {
            for (int i = 0; i < bytesRead; ++i) {
                char c = buffer[i];
                ++chars;

                if (c == '\n') {
                    ++lines;
                    lastCharWasNewline = true;
                } else {
                    lastCharWasNewline = false;
                }

                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                    inWord = false;
                } else if (!inWord) {
                    ++words;
                    inWord = true;
                }
            }
        }

        if (!lastCharWasNewline) {
            ++lines;
        }

        if (bytesRead == -1) {
            close(fd);
            return {1, "", "wc: error reading file '" + filename + "': " + strerror(errno)};
        }

        close(fd);

        if (countLines) out += std::to_string(lines) + " ";
        if (countWords) out += std::to_string(words) + " ";
        if (countChars) out += std::to_string(chars) + " ";
        out += filename + "\n";
    }

    return {0, stripTrailingNewline(out), ""};
}

/**
 * @brief Creates a new directory at the specified path.
 * @param args Directories and optional -p flag  
 * - Supports "-p" flag to recursively create each directory specified in the path
 * @return Status code, empty output on success, error message on failure
 */
CommandResult Commands::mkdirCommand(const std::vector<std::string>& args) {
    if (args.empty()) {
        return {1, "", "mkdir: missing directory argument"};
    }

    bool recurse = false;
    int idx = 0;

    while (idx < args.size() && args[idx][0] == '-') {
        if (args[idx] == "-p") {
            recurse = true;
        } else {
            return {1, "", "mkdir: invalid option '" + args[idx] + "'"};
        }
        idx++;
    }

    if (idx >= args.size()) {
        return {1, "", "mkdir: missing directory argument"};
    }

    for (; idx < args.size(); ++idx) {
        const std::string& path = args[idx];

        if (!recurse) {
            if (mkdir(path.c_str(), 0755) == -1) {
                return {1, "", "mkdir: cannot create directory '" + path + "': " + std::string(strerror(errno))};
            }
            continue;
        }

        std::string partial;
        for (size_t i = 0; i < path.size(); ++i) {
            partial += path[i];

            if (partial.back() != '/' && i != path.size() - 1) continue;

            if (partial == "/" || partial == "//") continue;

            if (mkdir(partial.c_str(), 0755) == -1) {
                if (errno != EEXIST) {
                    return {1, "", "mkdir: cannot create directory '" + partial + "': " + std::string(strerror(errno))};
                }
            }
        }
    }

    return {0, "", ""};
}

/**
 * @brief Removes a file or directory tree.
 * @param args A file or directory path, with optional flags:
 *        "-r" Recursively remove a directory and its contents
 * @return Status code, empty output on success, error message on failure
 */
CommandResult Commands::rmCommand(const std::vector<std::string>& args) {
    if (args.empty()) {
        return {1, "", "rm: missing operand"};
    }

    bool recursive = false;
    int currentArg = 0;

    while (currentArg < args.size() && args[currentArg][0] == '-') {
        const std::string& flag = args[currentArg];
        if (flag.find('r') != std::string::npos) {
            recursive = true;
        } else {
            return {1, "", "rm: invalid option '" + flag + "'"};
        }
        
        ++currentArg;
        if (currentArg == args.size()) {
            return {1, "", "rm: missing operand after '" + flag + "'"};
        }
    }

    std::string out;

    for (; currentArg < args.size(); ++currentArg) {
        const std::string& path = args[currentArg];

        struct stat st;
        if (stat(path.c_str(), &st) == -1) {
            return {1, "", "rm: cannot access '" + path + "': " + strerror(errno)};
        }

        if (S_ISDIR(st.st_mode)) {
            if (recursive) {
                DIR* dir = opendir(path.c_str());
                if (!dir) {
                    return {1, "", "rm: cannot open directory '" + path + "': " + strerror(errno)};
                }

                struct dirent* entry;
                while ((entry = readdir(dir)) != nullptr) {
                    std::string name = entry->d_name;
                    if (name == "." || name == "..") continue;
                    std::string subpath = path + "/" + name;
                    rmCommand({recursive ? "-r" : "", subpath});
                }
                closedir(dir);

                if (rmdir(path.c_str()) == -1) {
                    return {1, "", "rm: failed to remove directory '" + path + "': " + strerror(errno)};
                }

            } else {
                return {1, "", "rm: '" + path + "' is a directory"};
            }
        } else {
            if (unlink(path.c_str()) == -1) {
                return {1, "", "rm: cannot remove '" + path + "': " + strerror(errno)};
            }
        }
    }

    return {0, out, ""};
}

/**
 * @brief Move files or directories from one location to another.
 * @param args Must contain exactly two arguments:
 *        - args[0]: Source file or directory path
 *        - args[1]: Destination path
 * @return Status code, empty output on success, or an error message on failure
 */
CommandResult Commands::mvCommand(const std::vector<std::string>& args) {
    if (args.size() != 2) {
        return {1, "", "mv: requires exactly two arguments: source and destination"};
    }

    const std::string& src = args[0];
    std::string dest = args[1];

    struct stat st;
    if (stat(dest.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        std::string filename = src.substr(src.find_last_of("/\\") + 1);
        if (dest.back() != '/' && dest.back() != '\\') {
            dest += "/";
        }
        dest += filename;
    }

    /**
     * rename() fails with EXDEV ("Invalid cross-device link") when the source 
     * and destination are on different filesystems (like on Docker bind mounts and volumes).
     * In this case, the correct fallback behavior is:
    *    1. Copy the source file to the destination path, and
    *    2. Remove the original file
    */
    if (std::rename(src.c_str(), dest.c_str()) != 0) {
        if (errno == EXDEV) {
            int in = open(src.c_str(), O_RDONLY);
            if (in == -1) {
                return {1, "", "mv: cannot open source file '" + src + "'"};
            }

            int out = open(dest.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (out == -1) {
                close(in);
                return {1, "", "mv: cannot create destination file '" + dest + "'"};
            }

            char buffer[4096];
            ssize_t bytesRead;
            while ((bytesRead = read(in, buffer, sizeof(buffer))) > 0) {
                if (write(out, buffer, bytesRead) != bytesRead) {
                    close(in);
                    close(out);
                    return {1, "", "mv: write error while copying to '" + dest + "'"};
                }
            }

            close(in);
            close(out);

            if (unlink(src.c_str()) != 0) {
                return {1, "", "mv: copied but failed to remove original '" + src + "'"};
            }

            return {0, "", ""};
        }

        return {1, "", "mv: failed to move '" + src + "' to '" + dest + "': " + strerror(errno)};
    }

    return {0, "", ""};
}

/**
 * @brief Modify file permissions for user, group, and others
 * @param args Expects two arguments: 
 *        - permissions (must be numeric)
 *        - file path
 * @return Status code, empty output on success, or error message on failure
 */
CommandResult Commands::chmodCommand(const std::vector<std::string>& args) {
    if (args.size() != 2) {
        return {1, "", "chmod: requires exactly two arguments: permissions and file"};
    }

    const std::string& perm = args[0];
    const std::string& filename = args[1];

    mode_t mode = 0;
    try {
        mode = std::stoi(perm, nullptr, 8);
    } catch (...) {
        return {1, "", "chmod: invalid permissions format"};
    }

    if (chmod(filename.c_str(), mode) != 0) {
        return {1, "", "chmod: failed to change permissions for '" + filename + "': " + strerror(errno)};
    }

    return {0, "", ""};
}

/* --- Helper Functions --- */
std::string Commands::formatLsLongListing(const std::string& name, const struct stat& info) {
    std::string out;

    char type = S_ISDIR(info.st_mode) ? 'd' : '-';
    out += type;

    out += ((info.st_mode & S_IRUSR) ? 'r' : '-');
    out += ((info.st_mode & S_IWUSR) ? 'w' : '-');
    out += ((info.st_mode & S_IXUSR) ? 'x' : '-');
    out += ((info.st_mode & S_IRGRP) ? 'r' : '-');
    out += ((info.st_mode & S_IWGRP) ? 'w' : '-');
    out += ((info.st_mode & S_IXGRP) ? 'x' : '-');
    out += ((info.st_mode & S_IROTH) ? 'r' : '-');
    out += ((info.st_mode & S_IWOTH) ? 'w' : '-');
    out += ((info.st_mode & S_IXOTH) ? 'x' : '-');
    out += " " + std::to_string(info.st_nlink);

    struct passwd* pw = getpwuid(info.st_uid);
    std::string user = pw ? pw->pw_name : std::to_string(info.st_uid);
    out += " " + user;

    struct group* gr = getgrgid(info.st_gid);
    std::string group = gr ? gr->gr_name : std::to_string(info.st_gid);
    out += " " + group;

    out += " " + std::to_string(info.st_size);

    char timebuf[64];
    struct tm* t = localtime(&info.st_mtime);
    strftime(timebuf, sizeof(timebuf), "%b %d %H:%M", t);
    out += " " + std::string(timebuf);

    out += " " + name + "\n";

    return out;
}

std::string Commands::formatRmdirErrorMsg(const std::string& path) {
    switch (errno) {
        case ENOTEMPTY:
        case EEXIST:
            return "rmdir: failed to remove '" + path + "': directory not empty";

        case ENOENT:
            return "rmdir: failed to remove '" + path + "': no such file or directory";

        case ENOTDIR:
            return "rmdir: failed to remove '" + path + "': not a directory";

        case EACCES:
        case EPERM:
            return "rmdir: failed to remove '" + path + "': permission denied";

        default:
            return "rmdir: failed to remove '" + path + "': " + std::string(strerror(errno));
    }
}

bool Commands::matchesPattern(const std::string& line, const std::regex& re, bool printOnlyMatch, std::string& outMatch) {
    std::smatch match;
    
    if (!std::regex_search(line, match, re)) {
        return false;
    }

    if (printOnlyMatch) {
        outMatch = match.str();
    } else {
        outMatch = line;
    }

    return true;
}

std::string Commands::stripTrailingNewline(const std::string& s) {
    if (!s.empty() && s.back() == '\n') {
        return s.substr(0, s.size() - 1);
    }
    return s;
}

bool Commands::isFileEmpty(const std::string& filename) {
    struct stat st;
    if (stat(filename.c_str(), &st) != 0) {
        return false;
    }
    return st.st_size == 0;
}
