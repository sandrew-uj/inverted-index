#include "searcher.h"

#include <algorithm>
#include <iostream>
#include <iterator>
#include <set>

void Searcher::parse_word(std::string_view & word) const
{
    size_t start = 0;
    for (; start < word.size() && !std::isalnum(word[start]); ++start) {
    }
    int end = word.size() - 1;
    size_t suff = 0;
    for (; end >= 0 && !std::isalnum(word[end]); --end, ++suff) {
    }
    word.remove_prefix(start);
    word.remove_suffix(std::min(word.size(), suff));
}

void Searcher::add_document(const Searcher::Filename & filename, std::istream & strm)
{
    cash.clear();
    if (m_fileidxs.find(filename) != m_fileidxs.end()) {
        return;
    }
    m_filenames[m_count] = filename;
    m_fileidxs[filename] = m_count;
    std::string word;
    size_t prev_hash = 0;
    while (strm >> word) {
        std::transform(word.begin(), word.end(), word.begin(), ::tolower);
        std::string_view word_sv(word);
        parse_word(word_sv);
        if (!word_sv.empty()) {
            size_t current_hash = std::hash<std::string_view>()(word_sv);
            auto & current_positions = m_index[current_hash][m_count];
            if (prev_hash > 0) {
                auto & prev_positions = m_index[prev_hash][m_count];
                prev_positions[prev_positions.size() - 1] = {current_hash, current_positions.size()};
            }
            current_positions.push_back({0, 0});
            prev_hash = current_hash;
        }
    }
    m_count++;
}

void Searcher::remove_document(const Searcher::Filename & filename)
{
    cash.clear();
    if (m_fileidxs.find(filename) == m_fileidxs.end()) {
        return;
    }

    for (const auto & it : m_index) {
        m_index[it.first].erase(m_fileidxs[filename]);
    }

    m_filenames.erase(m_fileidxs[filename]);
    m_fileidxs.erase(filename);
}

std::pair<Searcher::DocIterator, Searcher::DocIterator> Searcher::search(const std::string & query) const
{
    std::vector<std::vector<size_t>> parse_result;
    parse_query(query, parse_result);

    size_t parse_result_hash = VectorHash()(parse_result);

    if (cash.find(parse_result_hash) != cash.end()) {
        return cash.at(parse_result_hash);
    }

    std::vector<int> result;
    int current_file = 0;

    while (current_file < m_count) {
        bool in_result = true;

        for (const auto & phrase : parse_result) {
            for (const size_t & current_word : phrase) {
                if (m_index.find(current_word) != m_index.end()) {
                    const auto & files = m_index.at(current_word);
                    auto f = files.lower_bound(current_file);
                    if (f == files.end()) {
                        in_result = false;
                        current_file = m_count;
                        break;
                    }
                    if (f->first != current_file) {
                        in_result = false;
                        current_file = f->first;
                    }
                }
                else {
                    in_result = false;
                    current_file = m_count;
                    break;
                }
            }

            if (!in_result) {
                break;
            }

            bool in_chain = false;
            const auto & files = m_index.at(phrase[0]).at(current_file);
            for (size_t i = 0; i < files.size() && !in_chain; ++i) {
                in_chain = true;
                size_t current_word = phrase[0];
                size_t current_pos = i;
                for (size_t k = 1; k < phrase.size() && in_chain; ++k) {
                    const auto & next = m_index.at(current_word).at(current_file)[current_pos];
                    in_chain = next.first == phrase[k];
                    current_pos = next.second;
                    current_word = phrase[k];
                }
            }

            if (!in_chain) {
                current_file++;
                in_result = false;
                break;
            }
        }

        if (in_result) {
            result.push_back(current_file++);
        }
    }

    cash[parse_result_hash] = {DocIterator(*this, 0, result), DocIterator(*this, result.size(), result)};

    return cash.at(parse_result_hash);
}
void Searcher::parse_query(std::string query, std::vector<std::vector<size_t>> & result) const
{
    size_t p = 0;
    bool is_open = false;

    if (!query.empty()) {
        std::transform(query.begin(), query.end(), query.begin(), ::tolower);
    }

    while (p < query.size()) {
        skip_whitespaces(query, p);

        if (!is_open && (result.empty() || !result[result.size() - 1].empty())) {
            result.emplace_back();
        }

        if (query[p] == '\"') {
            is_open = !is_open;
            p++;
            if (!is_open) {
                skip_whitespaces(query, p);
                continue;
            }
        }

        std::string_view word = get_word(query, p);
        parse_word(word);

        if (!word.empty()) {
            size_t word_hash = std::hash<std::string_view>()(word);
            result[result.size() - 1].push_back(word_hash);
        }
        skip_whitespaces(query, p);
    }

    if (!result.empty() && result[result.size() - 1].empty()) {
        result.pop_back();
    }

    if (result.empty()) {
        throw BadQuery("Empty query");
    }

    if (is_open) {
        throw BadQuery("Unclosed quote");
    }
}
void Searcher::skip_whitespaces(const std::string & s, size_t & p) const
{
    for (; p < s.size() && std::isspace(s[p]); ++p)
        ;
}

std::string_view Searcher::get_word(const std::string & s, size_t & p) const
{
    std::string_view result(s);
    result.remove_prefix(p);
    for (; p < s.size() && s[p] != '\"' && !std::isspace(s[p]); p++) {
    };
    result.remove_suffix(s.size() - p);
    return result;
}

std::ostream & operator<<(std::ostream & os, const Searcher & searcher)
{
    for (const auto & it : searcher.m_index) {
        os << "{" << it.first << ":" << std::endl;
        for (const auto & it1 : it.second) {
            os << "(" << it1.first << ":";
            for (size_t j = 0; j < it1.second.size(); ++j) {
                os << "[ " << it1.second[j].first << ", " << it1.second[j].second << "] ";
            }
            os << ")" << std::endl;
        }
        os << "}" << std::endl;
    }
    return os;
}

Searcher::BadQuery::BadQuery()
    : message("Search query syntax error: ")
{
}

Searcher::BadQuery::BadQuery(const std::string & message)
    : message("Search query syntax error: " + message)
{
}
const char * Searcher::BadQuery::what() const noexcept
{
    return &*message.begin();
}