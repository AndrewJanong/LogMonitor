#include "AhoCorasick.h"
#include <queue>

AhoCorasick::AhoCorasick(const std::vector<std::string>& patterns) {
    build(patterns);
}

void AhoCorasick::build(const std::vector<std::string>& patterns) {
    nodes.clear();
    nodes.emplace_back(); // root

    // build trie
    for (const auto& p : patterns) {
        int v = 0;
        for (char ch : p) {
            unsigned char c = static_cast<unsigned char>(ch);
            int& nxt = nodes[v].next[c];
            if (nxt == -1) {
                nxt = static_cast<int>(nodes.size());
                nodes.emplace_back();
            }
            v = nxt;
        }
        nodes[v].output = true;
    }

    // build failure links and complete transitions
    std::queue<int> q;
    for (int c = 0; c < 256; ++c) {
        int nxt = nodes[0].next[c];
        if (nxt != -1) {
            nodes[nxt].fail = 0;
            q.push(nxt);
        } else {
            nodes[0].next[c] = 0;
        }
    }

    while (!q.empty()) {
        int v = q.front();
        q.pop();
        for (int c = 0; c < 256; ++c) {
            int nxt = nodes[v].next[c];
            if (nxt != -1) {
                nodes[nxt].fail = nodes[nodes[v].fail].next[c];
                nodes[nxt].output =
                    nodes[nxt].output || nodes[nodes[nxt].fail].output;
                q.push(nxt);
            } else {
                nodes[v].next[c] = nodes[nodes[v].fail].next[c];
            }
        }
    }
}

bool AhoCorasick::matches(std::string_view text) const {
    int state = 0;
    for (char ch : text) {
        unsigned char c = static_cast<unsigned char>(ch);
        state = nodes[state].next[c];
        if (nodes[state].output) {
            return true; // found at least one pattern
        }
    }
    return false;
}
