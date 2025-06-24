#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <queue>
#include <ctime>
#include <cstdlib>
#include <sys/stat.h>
#include <filesystem>
#include <iomanip>

namespace fs = std::filesystem;

// ========== UTILITY FUNCTIONS ==========

bool directoryExists(const std::string& path) {
    struct stat info;
    return stat(path.c_str(), &info) == 0 && (info.st_mode & S_IFDIR);
}

bool fileExists(const std::string& path) {
    std::ifstream f(path.c_str());
    return f.good();
}

void createDirectories() {
    if (!directoryExists(".minigit"))
        system("mkdir .minigit");
    if (!directoryExists(".minigit\\objects"))
        system("mkdir .minigit\\objects");
    if (!directoryExists(".minigit\\commits"))
        system("mkdir .minigit\\commits");
    if (!directoryExists(".minigit\\refs"))
        system("mkdir .minigit\\refs");
}

void ensureRefsDirectory() {
    if (!directoryExists(".minigit")) {
        system("mkdir .minigit");
    }
    if (!directoryExists(".minigit\\refs")) {
        system("mkdir .minigit\\refs");
    }
}

std::string simpleHash(const std::string& content) {
    unsigned long hash = 5381;
    for (char c : content)
        hash = ((hash << 5) + hash) + c;
    
    std::ostringstream oss;
    oss << std::hex << hash;
    return oss.str();
}

std::string getCurrentTimestamp() {
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
    return std::string(buf);
}

std::string readHEAD() {
    std::ifstream head(".minigit\\HEAD");
    std::string line;
    getline(head, line);

    if (line.find("ref: ") == 0) {
        std::ifstream ref(".minigit\\" + line.substr(5));
        getline(ref, line);
    }
    return line;
}

std::string getParentCommitHash() {
    std::ifstream head(".minigit\\HEAD");
    std::string parent;
    if (head >> parent)
        return parent;
    return "none";
}

std::string getBranchHash(const std::string& branch) {
    std::ifstream file(".minigit\\refs\\" + branch);
    std::string hash;
    if (file >> hash) return hash;
    return "";
}

std::string extractField(const std::string& line) {
    size_t colon = line.find(":");
    if (colon == std::string::npos) return "";
    return line.substr(colon + 2); // skip ": "
}

// ========== BLOB STORAGE ==========

void storeBlob(const std::string& filename) {
    if (!fs::exists(filename)) {
        std::cerr << "? File does not exist: " << filename << "\n";
        return;
    }

    std::ifstream file(filename);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();

    std::string hash = simpleHash(content);

    fs::create_directories(".minigit/objects");

    std::string blobPath = ".minigit/objects/" + hash;
    if (!fs::exists(blobPath)) {
        std::ofstream blob(blobPath);
        blob << content;
        blob.close();
        std::cout << "? Blob stored at: " << blobPath << "\n";
    } else {
        std::cout << "? Blob already exists: " << blobPath << "\n";
    }
}

void storeBlobAndStage(const std::string& filename) {
    std::ifstream file(filename.c_str());
    if (!file) {
        std::cerr << "? Error: File not found: " << filename << "\n";
        return;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();

    std::string hash = simpleHash(content);
    std::string blobPath = ".minigit\\objects\\" + hash;

    // Save blob if it doesn't exist
    std::ifstream check(blobPath.c_str());
    if (!check) {
        std::ofstream out(blobPath.c_str());
        out << content;
        out.close();
        std::cout << "? Blob saved: " << blobPath << "\n";
    } else {
        std::cout << "? Blob already exists: " << blobPath << "\n";
    }

    // Append to index
    std::ofstream index(".minigit\\index", std::ios::app);
    index << filename << " " << hash << "\n";
    index.close();

    std::cout << "?? Snapshot staged in .minigit/index\n";
}

// ========== BRANCH MANAGEMENT ==========

void createPointer(const std::string& name) {
    std::ifstream headFile(".minigit\\HEAD");
    std::string hash;

    if (!(headFile >> hash)) {
        std::cerr << "? No HEAD found.\n";
        return;
    }

    ensureRefsDirectory();

    std::ofstream ref(".minigit\\refs\\" + name);
    ref << hash;
    ref.close();

    std::cout << "? Pointer '" << name << "' created ? " << hash << "\n";
}

void createBranch(const std::string& branchName) {
    std::ifstream head(".minigit\\HEAD");
    std::string currentHash;

    if (!head || !(head >> currentHash)) {
        std::cerr << "? HEAD not found or unreadable.\n";
        return;
    }

    ensureRefsDirectory();

    std::string path = ".minigit\\refs\\" + branchName;
    std::ofstream branch(path.c_str());

    if (!branch.is_open()) {
        std::cerr << "? Failed to create branch file at: " << path << "\n";
        return;
    }

    branch << currentHash;
    branch.close();

    std::cout << "? Branch '" << branchName << "' created ? " << currentHash << "\n";
}

// ========== COMMIT MANAGEMENT ==========

std::map<std::string, std::string> readBlobsFromCommit(const std::string& hash) {
    std::map<std::string, std::string> blobs;
    std::ifstream file(".minigit\\commits\\" + hash);
    std::string line;
    bool inBlobs = false;

    while (getline(file, line)) {
        if (line == "blobs:") {
            inBlobs = true;
            continue;
        }
        if (inBlobs && line.find("  ") == 0) {
            std::istringstream iss(line);
            std::string _, filename, blob;
            iss >> _ >> filename >> blob;
            blobs[filename] = blob;
        }
    }
    return blobs;
}

std::vector<std::string> readBlobLines(const std::string& blobHash) {
    std::ifstream file(".minigit\\objects\\" + blobHash);
    std::vector<std::string> lines;
    std::string line;
    while (getline(file, line)) {
        lines.push_back(line);
    }
    return lines;
}

void writeCommit(const std::string& message) {
    std::ifstream index(".minigit\\index");
    if (!index) {
        std::cerr << "? No staged files found.\n";
        return;
    }

    std::ostringstream content;
    std::string line;

    // Metadata
    content << "timestamp: " << getCurrentTimestamp() << "\n";
    content << "message: " << message << "\n";
    content << "parent: " << getParentCommitHash() << "\n";
    content << "blobs:\n";

    // File list from index
    while (getline(index, line)) {
        content << "  " << line << "\n";
    }
    index.close();

    // Generate commit hash
    std::string commitData = content.str();
    std::string commitHash = simpleHash(commitData);

    // Write to .minigit/commits/<hash>
    std::string path = ".minigit\\commits\\" + commitHash;
    std::ofstream out(path.c_str());
    out << commitData;
    out.close();

    // Update HEAD
    std::ofstream head(".minigit\\HEAD");
    head << commitHash;
    head.close();

    // Clear index
    std::ofstream clear(".minigit\\index", std::ios::trunc);
    clear.close();

    std::cout << "? Commit saved: " << commitHash << "\n";
    std::cout << "?? HEAD updated.\n";
}

// ========== CHECKOUT FUNCTIONALITY ==========

std::string resolveCommit(const std::string& input) {
    std::string refPath = ".minigit\\refs\\" + input;
    if (fileExists(refPath)) {
        std::ifstream ref(refPath.c_str());
        std::string hash;
        getline(ref, hash);
        return hash;
    }
    std::string commitPath = ".minigit\\commits\\" + input;
    if (fileExists(commitPath)) return input;
    return "";
}

void restoreWorkingDirectory(const std::string& commitHash) {
    std::string commitPath = ".minigit\\commits\\" + commitHash;
    std::ifstream commit(commitPath.c_str());

    if (!commit) {
        std::cerr << "? Commit not found: " << commitHash << "\n";
        return;
    }

    std::string line;
    bool readingBlobs = false;

    std::cout << "?? Restoring working directory...\n";

    while (getline(commit, line)) {
        if (line == "blobs:") {
            readingBlobs = true;
            continue;
        }

        if (readingBlobs && line.find("  ") == 0) {
            std::istringstream iss(line);
            std::string indent, filename, blobHash;
            iss >> indent >> filename >> blobHash;

            std::string blobPath = ".minigit\\objects\\" + blobHash;
            std::ifstream blob(blobPath.c_str());
            if (!blob) {
                std::cerr << "?? Missing blob: " << blobHash << "\n";
                continue;
            }

            std::ofstream outFile(filename.c_str());
            outFile << blob.rdbuf();
            outFile.close();
            blob.close();

            std::cout << "? Restored: " << filename << "\n";
        }
    }
}

void updateHEAD(const std::string& input) {
    std::string refPath = ".minigit\\refs\\" + input;
    std::ofstream head(".minigit\\HEAD");

    if (fileExists(refPath)) {
        head << "ref: refs/" << input;
        std::cout << "?? HEAD now points to branch: " << input << "\n";
    } else {
        head << input;
        std::cout << "?? HEAD now points to commit: " << input << "\n";
    }
    head.close();
}

void checkoutBranch(const std::string& branchName) {
    std::string refPath = ".minigit\\refs\\" + branchName;

    if (!fileExists(refPath)) {
        std::cerr << "? Branch '" << branchName << "' not found.\n";
        return;
    }

    std::ifstream ref(refPath.c_str());
    std::string commitHash;
    getline(ref, commitHash);
    ref.close();

    restoreWorkingDirectory(commitHash);
    updateHEAD(branchName);
}

void checkoutCommit(const std::string& commitHash) {
    std::string commitPath = ".minigit\\commits\\" + commitHash;
    if (!fileExists(commitPath)) {
        std::cerr << "? Commit not found: " << commitHash << "\n";
        return;
    }

    restoreWorkingDirectory(commitHash);
    updateHEAD(commitHash);
}

// ========== DIFF VIEWER ==========

void diffFiles(const std::string& filename,
               const std::vector<std::string>& oldLines,
               const std::vector<std::string>& newLines)
{
    std::cout << "\n--- " << filename << " (old)\n";
    std::cout << "+++ " << filename << " (new)\n";

    size_t i = 0, j = 0;
    while (i < oldLines.size() || j < newLines.size()) {
        if (i < oldLines.size() && j < newLines.size()) {
            if (oldLines[i] == newLines[j]) {
                std::cout << "  " << oldLines[i] << "\n";
                ++i; ++j;
            } else {
                std::cout << "- " << oldLines[i] << "\n";
                std::cout << "+ " << newLines[j] << "\n";
                ++i; ++j;
            }
        } else if (i < oldLines.size()) {
            std::cout << "- " << oldLines[i++] << "\n";
        } else if (j < newLines.size()) {
            std::cout << "+ " << newLines[j++] << "\n";
        }
    }
}

void showDiff() {
    std::string hash1, hash2;
    std::cout << "Enter first commit hash: ";
    std::cin >> hash1;
    std::cout << "Enter second commit hash: ";
    std::cin >> hash2;

    auto blobs1 = readBlobsFromCommit(hash1);
    auto blobs2 = readBlobsFromCommit(hash2);

    std::set<std::string> allFiles;
    for (auto& b : blobs1) allFiles.insert(b.first);
    for (auto& b : blobs2) allFiles.insert(b.first);

    for (const auto& file : allFiles) {
        std::string blob1 = blobs1.count(file) ? blobs1[file] : "";
        std::string blob2 = blobs2.count(file) ? blobs2[file] : "";

        auto lines1 = blob1.empty() ? std::vector<std::string>{} : readBlobLines(blob1);
        auto lines2 = blob2.empty() ? std::vector<std::string>{} : readBlobLines(blob2);

        diffFiles(file, lines1, lines2);
    }
}

// ========== LOG HISTORY ==========

void showLog(bool oneline = false) {
    std::ifstream headFile(".minigit\\HEAD");
    std::string commitHash;

    if (!(headFile >> commitHash)) {
        std::cout << "?? No commits found.\n";
        return;
    }

    while (commitHash != "none") {
        std::string path = ".minigit\\commits\\" + commitHash;
        std::ifstream commitFile(path.c_str());

        if (!commitFile) {
            std::cerr << "? Commit file not found: " << commitHash << "\n";
            break;
        }

        std::string line, timestamp, message, parent;

        // Parse lines to extract info
        while (getline(commitFile, line)) {
            if (line.find("timestamp:") == 0)
                timestamp = extractField(line);
            else if (line.find("message:") == 0)
                message = extractField(line);
            else if (line.find("parent:") == 0) {
                parent = extractField(line);
                break; // no need to read further
            }
        }

        // Display based on mode
        if (oneline) {
            std::cout << commitHash << " - " << message << "\n";
        } else {
            std::cout << "?? Commit: " << commitHash << "\n";
            std::cout << "?? " << timestamp << "\n";
            std::cout << "?? " << message << "\n\n";
        }

        // Move to parent
        commitHash = parent;
    }
}

// ========== MERGE FUNCTIONALITY ==========

std::set<std::string> getAncestors(const std::string& root) {
    std::set<std::string> visited;
    std::queue<std::string> q;
    q.push(root);

    while (!q.empty()) {
        std::string current = q.front(); q.pop();
        if (visited.count(current)) continue;
        visited.insert(current);

        std::ifstream commit(".minigit\\commits\\" + current);
        if (!commit) continue;

        std::string line;
        while (getline(commit, line)) {
            if (line.rfind("parent: ", 0) == 0) {
                q.push(line.substr(8));
            }
            if (line.rfind("parent2: ", 0) == 0) {
                q.push(line.substr(9));
            }
        }
    }
    return visited;
}

std::string findLCA(const std::string& h1, const std::string& h2) {
    auto a1 = getAncestors(h1);
    std::queue<std::string> q;
    q.push(h2);
    std::set<std::string> visited;

    while (!q.empty()) {
        std::string current = q.front(); q.pop();
        if (visited.count(current)) continue;
        visited.insert(current);
        if (a1.count(current)) return current;

        std::ifstream file(".minigit\\commits\\" + current);
        std::string line;
        while (getline(file, line)) {
            if (line.rfind("parent: ", 0) == 0) q.push(line.substr(8));
            if (line.rfind("parent2: ", 0) == 0) q.push(line.substr(9));
        }
    }
    return "";
}

void simpleMerge(const std::string& branchName) {
    std::string headHash = readHEAD();
    std::string branchHash = getBranchHash(branchName);

    if (branchHash.empty()) {
        std::cerr << "? Branch not found: " << branchName << "\n";
        return;
    }

    auto headBlobs = readBlobsFromCommit(headHash);
    auto branchBlobs = readBlobsFromCommit(branchHash);

    // Merge: prefer branch version if duplicate
    for (auto& entry : branchBlobs) {
        headBlobs[entry.first] = entry.second;
    }

    // Create merge commit
    std::ostringstream commitContent;
    time_t now = time(NULL);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));

    commitContent << "timestamp: " << buf << "\n";
    commitContent << "message: Merged branch '" << branchName << "'\n";
    commitContent << "parent: " << headHash << "\n";
    commitContent << "parent2: " << branchHash << "\n";
    commitContent << "blobs:\n";
    for (auto& pair : headBlobs) {
        commitContent << "  " << pair.first << " " << pair.second << "\n";
    }

    std::string content = commitContent.str();
    std::string newHash = simpleHash(content);

    std::ofstream out(".minigit\\commits\\" + newHash);
    out << content;
    out.close();

    // Update HEAD
    std::ofstream headFile(".minigit\\HEAD");
    headFile << newHash;
    headFile.close();

    std::cout << "? Simple merge complete: " << newHash << "\n";
}

std::map<std::string, std::string> threeWayMerge(
    const std::map<std::string, std::string>& base,
    const std::map<std::string, std::string>& current,
    const std::map<std::string, std::string>& target)
{
    std::map<std::string, std::string> result;
    std::set<std::string> allFiles;

    // Collect all file names
    for (auto& b : base) allFiles.insert(b.first);
    for (auto& c : current) allFiles.insert(c.first);
    for (auto& t : target) allFiles.insert(t.first);

    for (auto& file : allFiles) {
        std::string baseHash = base.count(file) ? base.at(file) : "";
        std::string currentHash = current.count(file) ? current.at(file) : "";
        std::string targetHash = target.count(file) ? target.at(file) : "";

        if (currentHash == targetHash || baseHash == targetHash) {
            result[file] = currentHash; // Unchanged or same as target
        } else if (baseHash == currentHash) {
            result[file] = targetHash; // Updated only in target
        } else if (baseHash == targetHash) {
            result[file] = currentHash; // Updated only in current
        } else {
            // Conflict: favor target side, or mark with a warning
            std::cerr << "?? Conflict in file: " << file << " ? using target version\n";
            result[file] = targetHash;
        }
    }
    return result;
}

void threeWayMerge(const std::string& targetBranch) {
    std::string currentHash = readHEAD();
    std::string targetHash = getBranchHash(targetBranch);
    std::string baseHash = findLCA(currentHash, targetHash);

    if (targetHash.empty() || baseHash.empty()) {
        std::cerr << "? Missing target branch or base commit.\n";
        return;
    }

    auto baseBlobs = readBlobsFromCommit(baseHash);
    auto currBlobs = readBlobsFromCommit(currentHash);
    auto targBlobs = readBlobsFromCommit(targetHash);

    auto mergedBlobs = threeWayMerge(baseBlobs, currBlobs, targBlobs);

    // Create merge commit
    std::ostringstream commitContent;
    time_t now = time(NULL);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));

    commitContent << "timestamp: " << buf << "\n";
    commitContent << "message: 3-way merge with branch '" << targetBranch << "'\n";
    commitContent << "parent: " << currentHash << "\n";
    commitContent << "parent2: " << targetHash << "\n";
    commitContent << "blobs:\n";
    for (auto& b : mergedBlobs) {
        commitContent << "  " << b.first << " " << b.second << "\n";
    }

    std::string content = commitContent.str();
    std::string newHash = simpleHash(content);

    std::ofstream commitFile(".minigit\\commits\\" + newHash);
    commitFile << content;
    commitFile.close();

    std::ofstream head(".minigit\\HEAD");
    head << newHash;
    head.close();

    std::cout << "? 3-way merge complete! New commit: " << newHash << "\n";
}

// ========== MAIN MENU ==========

void showMainMenu() {
    std::cout << "\nMiniGit Version Control System\n";
    std::cout << "1. Blob Storage\n";
    std::cout << "2. Branch Management\n";
    std::cout << "3. Commit Management\n";
    std::cout << "4. Checkout\n";
    std::cout << "5. Diff Viewer\n";
    std::cout << "6. Log History\n";
    std::cout << "7. Merge\n";
    std::cout << "8. Exit\n";
    std::cout << "Choose option: ";
}

void showBlobMenu() {
    std::cout << "\nMiniGit Blob Storage\n";
    std::cout << "1. Store file as blob\n";
    std::cout << "2. Stage file for snapshot\n";
    std::cout << "3. Back to main menu\n";
    std::cout << "Choose option: ";
}

void showBranchMenu() {
    std::cout << "\nMiniGit Branch Management\n";
    std::cout << "1. Create branch/pointer\n";
    std::cout << "2. Back to main menu\n";
    std::cout << "Choose option: ";
}

void showCheckoutMenu() {
    std::cout << "\nMiniGit Checkout\n";
    std::cout << "1. Checkout branch\n";
    std::cout << "2. Checkout commit\n";
    std::cout << "3. Back to main menu\n";
    std::cout << "Choose option: ";
}

void showMergeMenu() {
    std::cout << "\nMiniGit Merge System\n";
    std::cout << "1. Simple merge (take branch changes)\n";
    std::cout << "2. 3-way merge (with conflict detection)\n";
    std::cout << "3. Find common ancestor\n";
    std::cout << "4. Back to main menu\n";
    std::cout << "Choose option: ";
}

int main() {
    createDirectories();

    int mainChoice, subChoice;
    std::string input, filename, branch, message;

    while (true) {
        showMainMenu();
        std::cin >> mainChoice;

        if (mainChoice == 8) break;

        switch (mainChoice) {
            case 1: // Blob Storage
                while (true) {
                    showBlobMenu();
                    std::cin >> subChoice;
                    if (subChoice == 3) break;
                    
                    std::cout << "Enter filename: ";
                    std::cin >> filename;
                    
                    if (subChoice == 1) {
                        storeBlob(filename);
                    } else if (subChoice == 2) {
                        storeBlobAndStage(filename);
                    } else {
                        std::cout << "Invalid option\n";
                    }
                }
                break;
                
            case 2: // Branch Management
                while (true) {
                    showBranchMenu();
                    std::cin >> subChoice;
                    if (subChoice == 2) break;
                    
                    if (subChoice == 1) {
                        std::cout << "Enter new branch/pointer name: ";
                        std::cin >> input;
                        createBranch(input);
                    } else {
                        std::cout << "Invalid option\n";
                    }
                }
                break;
                
            case 3: // Commit Management
                std::cout << "Enter commit message: ";
                std::cin.ignore();
                std::getline(std::cin, message);
                writeCommit(message);
                break;
                
            case 4: // Checkout
                while (true) {
                    showCheckoutMenu();
                    std::cin >> subChoice;
                    if (subChoice == 3) break;
                    
                    std::cout << "Enter target: ";
                    std::cin >> input;
                    
                    if (subChoice == 1) {
                        checkoutBranch(input);
                    } else if (subChoice == 2) {
                        checkoutCommit(input);
                    } else {
                        std::cout << "Invalid option\n";
                    }
                }
                break;
                
            case 5: // Diff Viewer
                showDiff();
                break;
                
            case 6: // Log History
                showLog();
                break;
                
            case 7: // Merge
                while (true) {
                    showMergeMenu();
                    std::cin >> subChoice;
                    if (subChoice == 4) break;
                    
                    std::cout << "Enter branch name: ";
                    std::cin >> branch;
                    
                    if (subChoice == 1) {
                        simpleMerge(branch);
                    } else if (subChoice == 2) {
                        threeWayMerge(branch);
                    } else if (subChoice == 3) {
                        std::string headHash = readHEAD();
                        std::string targetHash = getBranchHash(branch);
                        if (targetHash.empty()) {
                            std::cerr << "? Branch not found\n";
                            break;
                        }
                        std::string lca = findLCA(headHash, targetHash);
                        if (!lca.empty()) {
                            std::cout << "? LCA commit: " << lca << "\n";
                        } else {
                            std::cout << "? No common ancestor found\n";
                        }
                    } else {
                        std::cout << "Invalid option\n";
                    }
                }
                break;
                
            default:
                std::cout << "Invalid option\n";
        }
    }

    return 0;
}
