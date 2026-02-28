#include "SearchEngine.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <cmath>
#include <thread>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <chrono>
namespace fs = std::filesystem;
using namespace std;






int SearchEngine::getDocumentCount() const {
    return documents.size();
}

int SearchEngine::getVocabularySize() const {
    return invertedIndex.size();
}





// ---------------- EDIT DISTANCE (LEVENSHTEIN) ----------------
int editDistance(const string& a, const string& b) {

    int n = a.size();
    int m = b.size();

    vector<vector<int>> dp(n + 1, vector<int>(m + 1));

    for (int i = 0; i <= n; i++)
        dp[i][0] = i;

    for (int j = 0; j <= m; j++)
        dp[0][j] = j;

    for (int i = 1; i <= n; i++) {
        for (int j = 1; j <= m; j++) {

            if (a[i - 1] == b[j - 1])
                dp[i][j] = dp[i - 1][j - 1];
            else {
                dp[i][j] = 1 + min({
                    dp[i - 1][j],     // delete
                    dp[i][j - 1],     // insert
                    dp[i - 1][j - 1]  // replace
                });
            }
        }
    }

    return dp[n][m];
}






// ---------------- SPELL CORRECTION ----------------
string correctWord(
    const string& queryWord,
    const unordered_map<string,
    unordered_map<int, Posting>>& index
){
    string bestWord = queryWord;
    int bestDist = INT_MAX;
    int bestDF = -1;

    for(const auto& [word, postingMap] : index){

        int dist = editDistance(queryWord, word);

        if(dist < bestDist){
            bestDist = dist;
            bestWord = word;
            bestDF = postingMap.size();
        }
        else if(dist == bestDist){
            int df = postingMap.size();

            if(df > bestDF){
                bestWord = word;
                bestDF = df;
            }
        }
    }

    return bestWord;
}







// ---------------- NORMALIZE ----------------

// ------- OLD Normalize Function
/*
static string normalize(const string& word) {
    string clean;
    for (char c : word)
        if (isalnum(static_cast<unsigned char>(c))) // isalpha -> isalnum
            clean += tolower(c);
    return clean;
}
*/

// ------- NEW Normalize Function
static string normalize(const string& word) {

    string clean;

    auto isAllowedSpecial = [](char c) {
        return c == '+' || c == '#' || c == '.' ||
               c == '-' || c == '_' || c == ':';
    };

    for (size_t i = 0; i < word.size(); i++) {

        char c = word[i];

        if (isalnum(static_cast<unsigned char>(c))) {
            clean += tolower(c);
        }
        else if (isAllowedSpecial(c)) {

            if (!clean.empty()) {
                clean += c;
            }
        }
    }

    // Remove trailing specials
    while (!clean.empty() &&
           !isalnum(static_cast<unsigned char>(clean.back()))) {
        clean.pop_back();
    }

    if (!clean.empty() &&
        !isalnum(static_cast<unsigned char>(clean.front()))) {
        clean.clear();
    }

    return clean;
}





// ---------------- SPLIT QUERY ----------------
static vector<string> splitQuery(const string& query) {
    vector<string> tokens;
    string word;
    stringstream ss(query);

    while (ss >> word) {
        word = normalize(word);
        if (!word.empty())
            tokens.push_back(word);
    }

    return tokens;
}

// ---------------- PHRASE MATCH ----------------
static int countPhraseOccurrences(
    const vector<int>& pos1,
    const vector<int>& pos2,
    vector<int>& phrasePositions
) {
    int i = 0, j = 0, count = 0;

    while (i < pos1.size() && j < pos2.size()) {

        if (pos2[j] == pos1[i] + 1) {
            count++;
            phrasePositions.push_back(pos1[i]);
            i++; j++;
        }
        else if (pos2[j] > pos1[i] + 1)
            i++;
        else
            j++;
    }

    return count;
}



static int countProximityMatches(
    const vector<int>& pos1,
    const vector<int>& pos2,
    int k,
    vector<int>& proxPositions
) {

    int i = 0, j = 0, count = 0;

    while (i < pos1.size() && j < pos2.size()) {

        int gap = abs(pos2[j] - pos1[i]) - 1;

        if (gap <= k && gap >= 0) {
            count++;
            proxPositions.push_back(pos1[i]);
            i++; j++;
        }
        else if (pos1[i] < pos2[j])
            i++;
        else
            j++;
    }

    return count;
}




// ---------------- ADD DOCUMENT PATH ----------------
void SearchEngine::addDocument(const string& path) {
    documents.push_back(path);
}


// ---------------- ADD DOCUMENT CONTENT ----------------
/*
void SearchEngine::addDocumentContent(const string& name, const string& content) {

    if (usingSample)
        clearIndex();

    documents.push_back(name);
    int docID = documents.size() - 1;

    documentContents[docID] = content;

    indexDocument(docID, content);

    

    // buildIndex();
}
*/

void SearchEngine::addDocumentContent(const string& name, const string& content) {

    string finalName = name;

    // 🔹 Handle duplicate filenames
    auto nameExists = [&](const string& checkName) {
        return find(documents.begin(), documents.end(), checkName) != documents.end();
    };

    if (nameExists(finalName)) {

        size_t dotPos = name.find_last_of('.');
        string base = (dotPos == string::npos) ? name : name.substr(0, dotPos);
        string ext  = (dotPos == string::npos) ? ""   : name.substr(dotPos);

        int counter = 1;
        while (nameExists(finalName)) {
            finalName = base + "(" + to_string(counter++) + ")" + ext;
        }
    }

    documents.push_back(finalName);
    int docID = documents.size() - 1;

    documentContents[docID] = content;

    // 🔹 Track vocabulary size before indexing
    size_t oldVocabSize = invertedIndex.size();

    // Incremental indexing
    indexDocument(docID, content);

    // 🔹 Update average document length
    double total = 0;
    for (auto& p : documentLength)
        total += p.second;

    if (!documentLength.empty())
        avgDocLength = total / documentLength.size();

    // 🔹 Insert only new words into Trie
    if (invertedIndex.size() > oldVocabSize) {
        for (auto& [word, postingMap] : invertedIndex) {
            trie.insert(word);
        }
    }
}



// ---------------- BUILD INDEX ----------------
/*
void SearchEngine::buildIndex() {

    for (int docID = 0; docID < documents.size(); docID++) {

        ifstream file(documents[docID]);
        if (!file) continue;

        stringstream buffer;
        buffer << file.rdbuf();

        documentContents[docID] = buffer.str();
        indexDocument(docID, buffer.str());
    }
}
*/


// ---- New BuildIndex for Threading 
void SearchEngine::buildIndex() {
    // just to check whether this function is called or not 
    // std::cout << "buildIndex() called\n";

    auto start = std::chrono::high_resolution_clock::now(); // To track time

    invertedIndex.clear();
    documentLength.clear();
    documentContents.clear();
    avgDocLength = 0.0;
    trie = Trie();


    int totalDocs = documents.size();
    if (totalDocs == 0) return;

    // Decide number of threads
    unsigned int numThreads = thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;
    numThreads = min(numThreads, (unsigned int)totalDocs);

    int chunkSize = (totalDocs + numThreads - 1) / numThreads;

    vector<thread> threads;

    // Per-thread local structures
    vector<unordered_map<string, unordered_map<int, Posting>>> localIndexes(numThreads);
    vector<unordered_map<int, int>> localDocLengths(numThreads);
    vector<unordered_map<int, string>> localContents(numThreads); // New

    for (unsigned int t = 0; t < numThreads; t++) {

        int start = t * chunkSize;
        int end = min(start + chunkSize, totalDocs);

        if (start >= totalDocs) break;

        threads.emplace_back([&, t, start, end]() {
            // To check threads are working or not you may comment if you dont like it 
            cout << "Thread Index: " << t
            << " | System Thread ID: "
            << this_thread::get_id()
            << " | Processing Docs: "
            << start << " to " << end - 1
            << endl;

            for (int docID = start; docID < end; docID++) {

                ifstream file(documents[docID]);
                if (!file) continue;

                stringstream buffer;
                buffer << file.rdbuf();

                string content = buffer.str();

                // Safe: each docID handled by exactly one thread
                // documentContents[docID] = content;
                localContents[t][docID] = content;

                indexDocumentLocal(
                    docID,
                    content,
                    localIndexes[t],
                    localDocLengths[t]
                );
            }
        });
    }

    // Wait for all threads
    for (auto& th : threads)
        th.join();

    // ---------------- MERGE PHASE ----------------
    for (unsigned int t = 0; t < threads.size(); t++) {

        // 🔥 Merge document contents first
        for (auto& [docID, content] : localContents[t]) {
            documentContents[docID] = content;
        }

        for (auto& [word, postingMap] : localIndexes[t]) {

            auto& globalPostingMap = invertedIndex[word];

            for (auto& [docID, posting] : postingMap) {
                globalPostingMap[docID] = posting;
            }
        }

        for (auto& [docID, length] : localDocLengths[t]) {
            documentLength[docID] = length;
        }
    }

    // Recompute average document length
    double totalLength = 0;
    for (auto& [docID, length] : documentLength)
        totalLength += length;

    avgDocLength = totalLength / documentLength.size();

    // Rebuild Trie after merge
    trie = Trie();
    for (auto& [word, _] : invertedIndex)
        trie.insert(word);
    
    auto end = std::chrono::high_resolution_clock::now();

    lastIndexingTimeMs =
        std::chrono::duration<double, std::milli>(end - start).count();

    lastThreadCount = threads.size();

    cout << "Index built in "
        << lastIndexingTimeMs
        << " ms using "
        << lastThreadCount
        << " threads."
        << endl;

}







// ---------------- INDEX DOCUMENT ----------------
void SearchEngine::indexDocument(int docID, const string& content) {

    stringstream ss(content);
    string word;
    int position = 0;
    long long offset = 0;

    while (ss >> word) {

        string clean = normalize(word);
        if (clean.empty()) continue;

        auto& posting = invertedIndex[clean][docID];
        posting.frequency++;
        posting.positions.push_back(position);
        posting.offsets.push_back(offset);

        trie.insert(clean);

        offset += word.length() + 1;
        position++;
    }


    documentLength[docID] = position;

    double total = 0;
    for(auto &p : documentLength)
        total += p.second;

    avgDocLength = total / documentLength.size();

}







// Local Indexing Function for Multithreading 

void SearchEngine::indexDocumentLocal(
    int docID,
    const string& content,
    unordered_map<string, unordered_map<int, Posting>>& localIndex,
    unordered_map<int, int>& localDocLength
) {
    stringstream ss(content);
    string word;
    int position = 0;
    long long offset = 0;

    while (ss >> word) {

        string clean = normalize(word);
        if (clean.empty()) continue;

        auto& posting = localIndex[clean][docID];
        posting.frequency++;
        posting.positions.push_back(position);
        posting.offsets.push_back(offset);

        offset += word.length() + 1;
        position++;
    }

    localDocLength[docID] = position;
}







double computeBM25(
    int tf,
    int df,
    int docLen,
    int N,
    double avgdl
){
    double k1 = 1.5;
    double b  = 0.75;

    double idf = log(1 + ((N - df + 0.5) / (df + 0.5)));
    

    double norm = (double)docLen / (double)avgdl;


    double num = tf * (k1 + 1.0);
    double den = tf + k1 *
        (1.0 - b + b * norm);

    return idf * (num / den);
}






// ======================= SEARCH API =======================
vector<SearchResult> SearchEngine::searchAPI(const string& query) {

    vector<SearchResult> results;
    vector<string> terms = splitQuery(query);


    string suggestedWord = "";

    for (string& term : terms) {
        if (invertedIndex.find(term) == invertedIndex.end()) {

            string corrected = correctWord(term, invertedIndex);

            if(corrected != term)
                suggestedWord = corrected;

            term = corrected;
        }
    }



    if (terms.empty()) return results;

    unordered_map<int, int> docPresence;

    for (const string& term : terms) {

        if (invertedIndex.find(term) == invertedIndex.end())
            return {};

        for (auto& [docID, posting] : invertedIndex[term]) {
            docPresence[docID]++;
        }
    }

    vector<int> candidateDocs;

    for (auto& [docID, count] : docPresence) {
        if (count == terms.size())   // ✔ must contain all terms
            candidateDocs.push_back(docID);
    }


    // -------- RESULT GENERATION --------
    for (int docID : candidateDocs){

        SearchResult res;
        res.document = documents[docID];
        res.suggestion = suggestedWord;


        string& content = documentContents[docID];



        double bm25Score = 0.0;
        int N = documents.size();

        for(const string& term : terms){

            if(invertedIndex[term].find(docID) == invertedIndex[term].end())
                continue;

            auto& posting = invertedIndex[term].at(docID);
            int tf = posting.frequency;

            int df = invertedIndex[term].size();
            int docLen = documentLength[docID];

            bm25Score += computeBM25(
                tf,
                df,
                docLen,
                N,
                avgDocLength
            );
        }

        res.score = bm25Score;



        if(terms.size() >= 2){

            auto& p1 = invertedIndex[terms[0]].at(docID);
            auto& p2 = invertedIndex[terms[1]].at(docID);

            vector<int> phrasePositions;

            int phraseFreq = countPhraseOccurrences(
                p1.positions,
                p2.positions,
                phrasePositions
            );

            if(phraseFreq > 0){
                res.score += 1.5 * phraseFreq;   // phrase boost
            }
        }



        if(terms.size() >= 2){

            auto& p1 = invertedIndex[terms[0]].at(docID);
            auto& p2 = invertedIndex[terms[1]].at(docID);

            vector<int> proxPositions;

            int proxFreq = countProximityMatches(
                p1.positions,
                p2.positions,
                3,
                proxPositions
            );

            if(proxFreq > 0){
                res.score += 0.75 * proxFreq;   // proximity boost
            }
        }




        // ================= SINGLE WORD =================
        if (terms.size() == 1) {

            auto& posting = invertedIndex[terms[0]][docID];
            res.frequency = posting.frequency;

            if (!posting.positions.empty()) {

                int idx = 0;
                long long offset = posting.offsets[idx];

                int start = max(0LL, offset - 60);
                int end = min((long long)content.size(), offset + 100);

                res.snippet = content.substr(start, end - start);
            }

            results.push_back(res);
            continue;
        }

        auto& posting = invertedIndex[terms[0]][docID];

        res.frequency = posting.frequency;

        if (!posting.positions.empty()) {

            int idx = 0;
            long long offset = posting.offsets[idx];

            int start = max(0LL, offset - 60);
            int end = min((long long)content.size(), offset + 100);

            res.snippet = content.substr(start, end - start);
        }


        results.push_back(res);
    }

    sort(results.begin(), results.end(),
    [](const SearchResult& a, const SearchResult& b) {
        return a.score > b.score;
    });


    return results;
}

// ---------------- AUTOCOMPLETE ----------------
vector<string> SearchEngine::autocompleteAPI(const string& prefix) {
    return trie.autocomplete(normalize(prefix));
}

// ---------------- CLEAR INDEX ----------------
void SearchEngine::clearIndex() {

    documents.clear();
    invertedIndex.clear();
    documentContents.clear();
    documentLength.clear();   // MISSING BEFORE
    avgDocLength = 0.0;       // RESET THIS TOO

    trie = Trie();

    usingSample = false;
    includeInitialCorpus = false; // New added for check
}



// ---------------- LOAD SAMPLE ----------------

void SearchEngine::loadSampleDataset() {

    clearIndex();   // 🔥 Always reset

    namespace fs = std::filesystem;

    string folderPath = "../documents";

    for (const auto& entry : fs::directory_iterator(folderPath)) {

        if (entry.is_regular_file() &&
            entry.path().extension() == ".txt") {

            documents.push_back(entry.path().string());
        }
    }

    if (documents.empty()) {
        cout << "No .txt files found in documents folder\n";
        return;
    }

    buildIndex();  // multithreaded

    includeInitialCorpus = true; // new addition for check 
}



double SearchEngine::getLastIndexingTime() const {
    return lastIndexingTimeMs;
}

int SearchEngine::getLastThreadCount() const {
    return lastThreadCount;
}




void SearchEngine::buildIndexSingleThread() {

    invertedIndex.clear();
    documentLength.clear();
    documentContents.clear();
    avgDocLength = 0.0;
    trie = Trie();

    for (int docID = 0; docID < documents.size(); docID++) {

        ifstream file(documents[docID]);
        if (!file) continue;

        stringstream buffer;
        buffer << file.rdbuf();

        string content = buffer.str();
        documentContents[docID] = content;

        indexDocument(docID, content);
    }

    // Recompute average doc length
    double totalLength = 0;
    for (auto& [docID, length] : documentLength)
        totalLength += length;

    if (!documentLength.empty())
        avgDocLength = totalLength / documentLength.size();

    // Build Trie
    for (auto& [word, _] : invertedIndex)
        trie.insert(word);
}





void SearchEngine::scanCorpusFolders() {

    namespace fs = std::filesystem;

    documents.clear();

    // Include permanent corpus ONLY if enabled
    if (includeInitialCorpus) {

        for (const auto& entry : fs::directory_iterator("../documents")) {
            if (entry.is_regular_file() &&
                entry.path().extension() == ".txt") {
                documents.push_back(entry.path().string());
            }
        }
    }

    // Always include runtime corpus
    for (const auto& entry : fs::directory_iterator("../runtime_corpus")) {
        if (entry.is_regular_file() &&
            entry.path().extension() == ".txt") {
            documents.push_back(entry.path().string());
        }
    }
}






void SearchEngine::indexSingleDocument(const string& path) {

    int docID = documents.size();
    documents.push_back(path);

    ifstream file(path);
    if (!file) return;

    stringstream buffer;
    buffer << file.rdbuf();
    string content = buffer.str();

    documentContents[docID] = content;

    indexDocument(docID, content);

    // Update average document length
    double total = 0;
    for (auto& p : documentLength)
        total += p.second;

    if (!documentLength.empty())
        avgDocLength = total / documentLength.size();

    // Insert words into Trie
    for (auto& [word, _] : invertedIndex)
        trie.insert(word);
}


