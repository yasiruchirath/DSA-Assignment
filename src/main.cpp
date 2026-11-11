#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

class PathQueue {
public:
    void push(std::string path) {
        auto node = std::make_unique<Node>(std::move(path));
        Node* newTail = node.get();
        if (tail_) {
            tail_->next = std::move(node);
        } else {
            head_ = std::move(node);
        }
        tail_ = newTail;
    }

    bool empty() const {
        return !head_;
    }

    std::string pop() {
        std::string path = std::move(head_->path);
        head_ = std::move(head_->next);
        if (!head_) {
            tail_ = nullptr;
        }
        return path;
    }

private:
    struct Node {
        explicit Node(std::string value) : path(std::move(value)) {}
        std::string path;
        std::unique_ptr<Node> next;
    };

    std::unique_ptr<Node> head_;
    Node* tail_ = nullptr;
};

struct FileRecord {
    std::string path;
    std::string content;
    std::string normalized;
    std::uint64_t fingerprint = 0;
};

std::string normalize(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    bool pendingSpace = false;

    for (unsigned char ch : text) {
        if (std::isspace(ch)) {
            pendingSpace = !result.empty();
        } else {
            if (pendingSpace) {
                result.push_back(' ');
                pendingSpace = false;
            }
            result.push_back(static_cast<char>(std::tolower(ch)));
        }
    }
    return result;
}

std::uint64_t fingerprint(const std::string& content) {
    constexpr std::uint64_t offsetBasis = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t hash = offsetBasis;
    for (unsigned char ch : content) {
        hash ^= ch;
        hash *= prime;
    }
    return hash;
}

std::set<std::string> tokens(const std::string& text) {
    std::set<std::string> result;
    std::string token;
    for (char ch : text) {
        if (ch == ' ') {
            if (!token.empty()) {
                result.insert(std::move(token));
                token.clear();
            }
        } else {
            token.push_back(ch);
        }
    }
    if (!token.empty()) {
        result.insert(std::move(token));
    }
    return result;
}

double jaccardSimilarity(const std::set<std::string>& left,
                         const std::set<std::string>& right) {
    if (left.empty() && right.empty()) {
        return 1.0;
    }

    std::size_t intersection = 0;
    for (const auto& token : left) {
        if (right.count(token) != 0) {
            ++intersection;
        }
    }
    const std::size_t unionSize = left.size() + right.size() - intersection;
    return static_cast<double>(intersection) / static_cast<double>(unionSize);
}

std::string displayPath(const std::string& path) {
    return std::filesystem::path(path).lexically_normal().string();
}

class DuplicateDetector {
public:
    void addPath(const std::string& path) {
        paths_.push_back(path);
        std::cout << "Queued: " << path << '\n';
    }

    void setNearDuplicateMode(bool enabled) {
        nearDuplicateMode_ = enabled;
    }

    bool nearDuplicateMode() const {
        return nearDuplicateMode_;
    }

    void scan() const {
        if (paths_.empty()) {
            std::cout << "No files registered. Add at least one file first.\n";
            return;
        }

        PathQueue queue;
        for (const auto& path : paths_) {
            queue.push(path);
        }

        std::list<FileRecord> files;
        while (!queue.empty()) {
            const std::string path = queue.pop();
            std::ifstream input(path, std::ios::binary);
            if (!input) {
                std::cerr << "Could not open file: " << path << '\n';
                continue;
            }

            std::string content((std::istreambuf_iterator<char>(input)),
                                std::istreambuf_iterator<char>());
            if (input.bad()) {
                std::cerr << "Could not read file: " << path << '\n';
                continue;
            }

            files.push_back(
                {path, content, normalize(content), fingerprint(content)});
        }

        if (files.empty()) {
            std::cout << "No readable files to compare.\n";
            return;
        }

        std::vector<const FileRecord*> records;
        records.reserve(files.size());
        for (const auto& file : files) {
            records.push_back(&file);
        }

        std::map<std::uint64_t, std::vector<std::size_t>> candidates;
        std::vector<std::vector<std::size_t>> duplicateGroups;
        for (std::size_t i = 0; i < records.size(); ++i) {
            auto& bucket = candidates[records[i]->fingerprint];
            bool matched = false;
            for (std::size_t groupIndex : bucket) {
                const std::size_t representative = duplicateGroups[groupIndex].front();
                if (records[i]->content == records[representative]->content) {
                    duplicateGroups[groupIndex].push_back(i);
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                bucket.push_back(duplicateGroups.size());
                duplicateGroups.push_back({i});
            }
        }

        reportFiles(records);
        reportExactDuplicates(records, duplicateGroups);
        if (nearDuplicateMode_) {
            reportNearDuplicates(records);
        }
    }

private:
    static void reportFiles(const std::vector<const FileRecord*>& records) {
        std::vector<const FileRecord*> sorted = records;
        std::sort(sorted.begin(), sorted.end(), [](const auto* left, const auto* right) {
            if (left->content.size() != right->content.size()) {
                return left->content.size() < right->content.size();
            }
            return displayPath(left->path) < displayPath(right->path);
        });

        std::cout << "\nReadable files (sorted by size, then name):\n";
        for (const auto* file : sorted) {
            std::cout << "  " << std::setw(8) << file->content.size()
                      << " bytes  " << displayPath(file->path) << '\n';
        }
    }

    static void reportExactDuplicates(
        const std::vector<const FileRecord*>& records,
        const std::vector<std::vector<std::size_t>>& groups) {
        std::size_t savings = 0;
        bool found = false;
        std::cout << "\nExact duplicate groups:\n";
        for (const auto& group : groups) {
            if (group.size() < 2) {
                continue;
            }
            found = true;
            std::cout << "  Fingerprint: 0x" << std::hex
                      << records[group.front()]->fingerprint << std::dec << '\n';
            for (std::size_t index : group) {
                std::cout << "    " << displayPath(records[index]->path) << '\n';
            }
            for (std::size_t i = 1; i < group.size(); ++i) {
                savings += records[group[i]]->content.size();
            }
        }
        if (!found) {
            std::cout << "  None found.\n";
        }
        std::cout << "Estimated storage savings if one copy per group is kept: "
                  << savings << " bytes\n";
    }

    static void reportNearDuplicates(
        const std::vector<const FileRecord*>& records) {
        constexpr double threshold = 0.80;
        std::vector<std::set<std::string>> tokenSets;
        tokenSets.reserve(records.size());
        for (const auto* record : records) {
            tokenSets.push_back(tokens(record->normalized));
        }

        std::cout << "\nNear-duplicate candidates (token Jaccard >= "
                  << static_cast<int>(threshold * 100) << "%):\n";
        bool found = false;
        for (std::size_t i = 0; i < records.size(); ++i) {
            for (std::size_t j = i + 1; j < records.size(); ++j) {
                if (records[i]->content == records[j]->content) {
                    continue;
                }
                const double similarity =
                    jaccardSimilarity(tokenSets[i], tokenSets[j]);
                if (similarity >= threshold) {
                    found = true;
                    std::cout << "  " << std::fixed << std::setprecision(1)
                              << similarity * 100.0 << "%  "
                              << displayPath(records[i]->path) << " <-> "
                              << displayPath(records[j]->path) << '\n';
                }
            }
        }
        if (!found) {
            std::cout << "  None found.\n";
        }
    }

    std::vector<std::string> paths_;
    bool nearDuplicateMode_ = false;
};

}  // namespace

int main() {
    DuplicateDetector detector;
    while (true) {
        std::cout << "\n1. Register a text file\n"
                  << "2. Scan registered files\n"
                  << "3. Toggle near-duplicate mode (currently "
                  << (detector.nearDuplicateMode() ? "on" : "off") << ")\n"
                  << "4. Exit\n"
                  << "Choice: ";

        std::string line;
        if (!std::getline(std::cin, line)) {
            break;
        }
        int choice = 0;
        try {
            std::size_t consumed = 0;
            choice = std::stoi(line, &consumed);
            if (consumed != line.size()) {
                throw std::invalid_argument("extra input");
            }
        } catch (const std::exception&) {
            std::cout << "Enter a number from 1 to 4.\n";
            continue;
        }

        if (choice == 1) {
            std::cout << "File path: ";
            std::string path;
            if (!std::getline(std::cin, path)) {
                break;
            }
            if (path.empty()) {
                std::cout << "File path cannot be empty.\n";
            } else {
                detector.addPath(path);
            }
        } else if (choice == 2) {
            detector.scan();
        } else if (choice == 3) {
            const bool enabled = !detector.nearDuplicateMode();
            detector.setNearDuplicateMode(enabled);
            std::cout << "Near-duplicate mode is now "
                      << (enabled ? "on" : "off") << ".\n";
        } else if (choice == 4) {
            break;
        } else {
            std::cout << "Enter a number from 1 to 4.\n";
        }
    }
    return 0;
}
