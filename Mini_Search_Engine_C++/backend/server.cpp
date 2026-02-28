#include "httplib.h"
#include "SearchEngine.h"
#include <iostream>
#include <fstream>
#include <chrono>   
#include<filesystem>
namespace fs = std::filesystem;

using namespace std;   


string escapeJson(const string& s) {
    string out;
    for (char c : s) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out;
}


// ---------------- JSON Helper ----------------
string toJson(const vector<SearchResult>& results) {
    string json = "{ \"results\": [";

    for (size_t i = 0; i < results.size(); i++) {
        const auto& r = results[i];

        json += "{";
        json += "\"document\":\"" + r.document + "\",";
        json += "\"suggestion\":\"" + r.suggestion + "\",";
        json += "\"frequency\":" + to_string(r.frequency) + ",";
        json += "\"score\":" + to_string(r.score) + ",";


        json += "\"positions\":[";
        for (size_t j = 0; j < r.positions.size(); j++) {
            json += to_string(r.positions[j]);
            if (j + 1 < r.positions.size()) json += ",";
        }
        json += "],";

        json += "\"offsets\":[";
        for (size_t j = 0; j < r.offsets.size(); j++) {
            json += to_string(r.offsets[j]);
            if (j + 1 < r.offsets.size()) json += ",";
        }
        json += "]";

        json += ",\"snippet\":\"" + escapeJson(r.snippet) + "\"";

        json += "}";
        if (i + 1 < results.size()) json += ",";
    }


    json += "] }";
    return json;
}

// ---------------- MAIN ----------------
int main() {
    SearchEngine engine;
    httplib::Server server;
    

    // STEP 1: Initialize runtime_corpus folder
    fs::remove_all("../runtime_corpus");
    fs::create_directory("../runtime_corpus");


// -------- Upload Endpoint --------
    server.Post("/upload", [&](const httplib::Request& req,
                            httplib::Response& res) {

        res.set_header("Access-Control-Allow-Origin", "*");

        if (req.body.empty()) {
            res.set_content("Empty file content", "text/plain");
            return;
        }

        if (!req.has_param("filename")) {
            res.set_content("Missing filename", "text/plain");
            return;
        }

        string docName = req.get_param_value("filename");

        string baseName = docName;
        string savePath = "../runtime_corpus/" + baseName;

        int counter = 1;

        while (fs::exists(savePath)) {

            size_t dotPos = baseName.find_last_of('.');
            string name = baseName;
            string extension = "";

            if (dotPos != string::npos) {
                name = baseName.substr(0, dotPos);
                extension = baseName.substr(dotPos);
            }

            savePath = "../runtime_corpus/" +
                    name + "_" + to_string(counter) + extension;

            counter++;
        }

        ofstream out(savePath);
        out << req.body;
        out.close();

        engine.indexSingleDocument(savePath);

        // 🔥 Back to plain text
        res.set_content("File uploaded and indexed successfully!", "text/plain");
    });


    // -------- Load Sample Dataset --------
    server.Get("/loadSample", [&](const httplib::Request& req,
                              httplib::Response& res) {

        engine.loadSampleDataset();

        string json = "{";
        json += "\"indexing_time_ms\":" +
                to_string(engine.getLastIndexingTime()) + ",";
        json += "\"threads_used\":" +
                to_string(engine.getLastThreadCount());
        json += "}";

        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(json, "application/json");
    });


    // -------- Search Endpoint --------
    server.Get("/search", [&](const httplib::Request& req,
                            httplib::Response& res) {

        if (!req.has_param("q")) {
            res.set_content("Missing query", "text/plain");
            return;
        }

        auto q = req.get_param_value("q");

        // Start timer
        auto start = std::chrono::high_resolution_clock::now();

        auto results = engine.searchAPI(q);

        // End timer
        auto end = std::chrono::high_resolution_clock::now();

        double latency =
            std::chrono::duration<double, std::milli>(end - start).count();

        // Build JSON
        string resultsJson = toJson(results);

        // Inject latency into JSON
        string finalJson = "{";
        finalJson += "\"latency_ms\":" + to_string(latency) + ",";
        finalJson += resultsJson.substr(1); // remove first '{'

        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(finalJson, "application/json");
    });

    
    // -------- Autocomplete Endpoint --------
    server.Get("/autocomplete", [&](const httplib::Request& req,
                                    httplib::Response& res) {

        if (!req.has_param("prefix")) {
            res.set_content("Missing prefix", "text/plain");
            return;
        }

        auto prefix = req.get_param_value("prefix");
        auto words = engine.autocompleteAPI(prefix);

        string json = "{ \"suggestions\": [";
        for (size_t i = 0; i < words.size(); i++) {
            json += "\"" + words[i] + "\"";
            if (i + 1 < words.size()) json += ",";
        }
        json += "] }";

        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(json, "application/json");
    });



    // -------- Corpus Info Endpoint --------
    server.Get("/corpusInfo", [&](const httplib::Request& req,
                                httplib::Response& res) {

        string json = "{";
        json += "\"documents\":" + to_string(engine.getDocumentCount()) + ",";
        json += "\"vocabulary\":" + to_string(engine.getVocabularySize());
        json += "}";

        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(json, "application/json");
    });


    // -------- Clear Corpus --------
    server.Post("/clearCorpus", [&](const httplib::Request& req,
                                httplib::Response& res) {

        namespace fs = std::filesystem;

        // 1. Clear in-memory index
        engine.clearIndex();

        // 2. Delete all runtime uploaded files
        for (const auto& entry : fs::directory_iterator("../runtime_corpus")) {
            if (entry.is_regular_file()) {
                fs::remove(entry.path());
            }
        }

        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content("Corpus cleared successfully", "text/plain");
    });


    // -------- Rebuild Index --------
    server.Post("/rebuildIndex", [&](const httplib::Request& req,
                                    httplib::Response& res) {

        engine.scanCorpusFolders();   // refresh document list based on mode
        engine.buildIndex();          // rebuild index structures

        string json = "{";
        json += "\"indexing_time_ms\":" +
                to_string(engine.getLastIndexingTime()) + ",";
        json += "\"threads_used\":" +
                to_string(engine.getLastThreadCount());
        json += "}";

        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(json, "application/json");
    });      


    server.Get("/benchmark", [&](const httplib::Request& req,
                             httplib::Response& res) {

        namespace fs = std::filesystem;

        vector<string> filePaths;

        // Scan permanent corpus
        for (const auto& entry : fs::directory_iterator("../documents")) {
            if (entry.is_regular_file() &&
                entry.path().extension() == ".txt") {
                filePaths.push_back(entry.path().string());
            }
        }

        // Scan runtime corpus
        for (const auto& entry : fs::directory_iterator("../runtime_corpus")) {
            if (entry.is_regular_file() &&
                entry.path().extension() == ".txt") {
                filePaths.push_back(entry.path().string());
            }
        }

        if (filePaths.empty()) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_content("No documents found for benchmark", "text/plain");
            return;
        }

        // ---------- SINGLE THREAD ----------
        SearchEngine singleEngine;

        for (auto& path : filePaths)
            singleEngine.addDocument(path);

        auto start1 = std::chrono::high_resolution_clock::now();
        singleEngine.buildIndexSingleThread();
        auto end1 = std::chrono::high_resolution_clock::now();

        double singleTime =
            std::chrono::duration<double, std::milli>(end1 - start1).count();


        // ---------- MULTI THREAD ----------
        SearchEngine multiEngine;

        for (auto& path : filePaths)
            multiEngine.addDocument(path);

        auto start2 = std::chrono::high_resolution_clock::now();
        multiEngine.buildIndex();
        auto end2 = std::chrono::high_resolution_clock::now();

        double multiTime =
            std::chrono::duration<double, std::milli>(end2 - start2).count();

        double speedup = singleTime / multiTime;

        string json = "{";
        json += "\"single_thread_ms\":" + to_string(singleTime) + ",";
        json += "\"multi_thread_ms\":" + to_string(multiTime) + ",";
        json += "\"threads_used\":" + to_string(multiEngine.getLastThreadCount()) + ",";
        json += "\"speedup\":" + to_string(speedup);
        json += "}";

        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(json, "application/json");
    });


    cout << "Dynamic Search Engine running at http://localhost:8080\n";
    server.listen("localhost", 8080);
}
