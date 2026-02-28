#ifndef SEARCH_ENGINE_H
#define SEARCH_ENGINE_H

#include <string>
#include <vector>
#include <unordered_map>
#include "Trie.h"

using namespace std;

struct Posting {
    int frequency = 0;
    vector<int> positions;
    vector<long long> offsets;
};

struct SearchResult {
    string document;
    int frequency;
    vector<int> positions;
    vector<long long> offsets;
    string snippet;   // NEW
    double score = 0.0;   // ⭐ TF-IDF SCORE
    string suggestion;
};


class SearchEngine {
public:
    // Add document from file path
    void buildIndexSingleThread();
    void addDocument(const string& path);
    int getDocumentCount() const;
    int getVocabularySize() const;
    void scanCorpusFolders();
    void indexSingleDocument(const string& path);

    // NEW: Add document directly from content
    void addDocumentContent(const string& name, const string& content);

    void buildIndex();
    void loadSampleDataset();
    void clearIndex();


    vector<SearchResult> searchAPI(const string& word);
    vector<string> autocompleteAPI(const string& prefix);

    double getLastIndexingTime() const;
    int getLastThreadCount() const;

private:
    vector<string> documents;
    unordered_map<int,int> documentLength;
    double lastIndexingTimeMs = 0.0;
    int lastThreadCount = 0;
    double avgDocLength = 0.0;

    unordered_map<string, unordered_map<int, Posting>> invertedIndex;
    Trie trie;
    unordered_map<int, string> documentContents; // New Addition to show snippets 
    bool usingSample = false;
    bool includeInitialCorpus = false; // new addition for check 

    void indexDocument(int docID, const string& content);


    // 🔥 NEW: Thread-safe local indexing helper
    void indexDocumentLocal(
        int docID,
        const string& content,
        unordered_map<string, unordered_map<int, Posting>>& localIndex,
        unordered_map<int, int>& localDocLength
    );       
    
};

#endif
