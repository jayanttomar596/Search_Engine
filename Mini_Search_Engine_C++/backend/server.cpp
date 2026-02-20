#include "httplib.h"
#include "SearchEngine.h"
#include <iostream>

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

    // -------- Upload Endpoint (Plain Text Version) --------
    server.Post("/upload", [&](const httplib::Request& req,
                               httplib::Response& res) {

        if (req.body.empty()) {
            res.set_content("Empty file content", "text/plain");
            return;
        }

        static int fileCounter = 1;
        string docName = "uploaded_doc_" + to_string(fileCounter++) + ".txt";

        engine.addDocumentContent(docName, req.body);

        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content("File uploaded and indexed successfully!", "text/plain");
    });


    // -------- Load Sample Dataset --------
    server.Get("/loadSample", [&](const httplib::Request& req,
                                httplib::Response& res) {

        engine.loadSampleDataset();

        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content("Sample dataset loaded", "text/plain");
    });


    // -------- Search Endpoint --------
    server.Get("/search", [&](const httplib::Request& req,
                              httplib::Response& res) {

        if (!req.has_param("q")) {
            res.set_content("Missing query", "text/plain");
            return;
        }

        auto q = req.get_param_value("q");
        auto results = engine.searchAPI(q);

        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(toJson(results), "application/json");
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

    cout << "Dynamic Search Engine running at http://localhost:8080\n";
    server.listen("0.0.0.0", 8080);
}
