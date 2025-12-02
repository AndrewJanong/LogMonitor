#ifndef AHO_CORASICK_H
#define AHO_CORASICK_H

#include <string>
#include <string_view>
#include <vector>

class AhoCorasick {
public:
    explicit AhoCorasick(const std::vector<std::string>& patterns);

    bool matches(std::string_view text) const;

private:
    struct Node {
        int next[256];
        int fail;
        bool output;
        Node() : fail(0), output(false) {
            for (int i = 0; i < 256; ++i) {
                next[i] = -1;
            }
        }
    };

    std::vector<Node> nodes;

    void build(const std::vector<std::string>& patterns);
};

#endif // AHO_CORASICK_H
