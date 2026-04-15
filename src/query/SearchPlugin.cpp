
#include "Plugin.h"
#include "SearchCommon.h"

#include <pthread.h>

static string html_escape(const string &s) {
    string out;
    for (size_t i = 0; i < s.size(); ++i) {
        switch (s[i]) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '\'':
            out += "&#39;";
            break;
        default:
            out += s[i];
        }
    }
    return out;
}

class SearchPlugin : public PluginObject {
  public:
    bool MagicPath(const string path) override { return path == "/" || path.find("/search") == 0; }

    string ProcessRequest(string req) override {

        // parse path from raw HTTP requests
        size_t space1 = req.find(' ');
        size_t space2 = req.find(' ', space1 + 1);
        if (space1 == string::npos || space2 == string::npos)
            return "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n"
                   "Connection: close\r\n\r\n";

        string full_path = req.substr(space1 + 1, space2 - space1 - 1);

        // redirect "/" to "/index.html"
        if (full_path == "/")
            return "HTTP/1.1 302 Found\r\n"
                   "Location: /index.html\r\n"
                   "Connection: close\r\n\r\n";

        // extract query parameter from /search?q=...
        string query = "";
        size_t q_pos = full_path.find("q=");
        if (q_pos != string::npos) {
            size_t start = q_pos + 2;
            size_t end = string::npos;
            for (size_t i = start; i < full_path.size(); ++i) {
                if (full_path[i] == ' ' || full_path[i] == '&' || full_path[i] == '#') {
                    end = i;
                    break;
                }
            }
            if (end == string::npos)
                end = full_path.size();
            const char *qbegin = full_path.c_str() + start;
            string raw_query(qbegin, qbegin + (end - start));
            query = url_decode(raw_query);
        }

        if (query.empty()) {
            return "HTTP/1.1 400 Bad Request\r\n"
                   "Content-Length: 27\r\n"
                   "Connection: close\r\n\r\n"
                   "Missing query parameter ?q=";
        }

        // Query worker nodes concurrently.
        // TODO: load worker addresses from a config file.
        ::vector<WorkerArgs> workers = {{"127.0.0.1", 8081, query, {}},
                                        {"127.0.0.1", 8082, query, {}}};

        ::vector<pthread_t> threads(workers.size());
        for (size_t i = 0; i < workers.size(); ++i)
            pthread_create(&threads[i], nullptr, fetch_from_worker, &workers[i]);
        for (size_t i = 0; i < threads.size(); ++i)
            pthread_join(threads[i], nullptr);

        // merge results using the broker's GlobalTopKHeap, jsut top 10
        GlobalTopKHeap top_k(10);
        for (const auto &w : workers)
            for (const auto &r : w.local_results)
                top_k.push(r);

        ::vector<GlobalResult> results = top_k.extractSorted();

        // html results
        string html = buildResultsPage(query, results);

        string http_response = "HTTP/1.1 200 OK\r\n"
                               "Content-Type: text/html; charset=utf-8\r\n"
                               "Content-Length: " +
                               to_string(html.size()) +
                               "\r\n"
                               "Connection: close\r\n\r\n" +
                               html;

        return http_response;
    }

    SearchPlugin() { Plugin = this; }

  private:
    string buildResultsPage(const string &query, const ::vector<GlobalResult> &results) {
        string h;
        h += "<!DOCTYPE html> <html><head>"
             "<meta charset='UTF-8'>"
             "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
             "<title>";
        h += html_escape(query);
        h += " - EECS 489 Search</title>"
             "<link rel='stylesheet' href='/style.css'>"
             "</head><body>"
             "<div class='header'>"
             "<h1><a href='/index.html'>EECS 489 Search</a></h1>"
             "<form action='/search' method='get' class='search-bar'>"
             "<input type='text' name='q' value='";
        h += html_escape(query);
        h += "' autofocus />"
             "<button type='submit'>&#128269;</button>"
             "</form></div>"
             "<div class='info'>";
        h += to_string(results.size());
        h += " result";
        if (results.size() != 1)
            h += "s";
        h += "</div>";

        if (results.empty()) {
            h += "<div class='no-results'>No results found for <b>";
            h += html_escape(query);
            h += "</b>.</div>";
        } else {
            h += "<div class='results'>";
            for (size_t i = 0; i < results.size(); ++i) {
                string display_url = results[i].url.size() > 70
                                         ? results[i].url.substr(0, 70) + "..."
                                         : results[i].url;
                string title = results[i].title.empty() ? results[i].url : results[i].title;

                h += "<div class='result'> "
                     "<div class='result-url'>";
                h += html_escape(display_url);
                h += "</div><div class='result-title'><a href='";
                h += html_escape(results[i].url);
                h += "' target='_blank'>";
                h += html_escape(title);
                h += "</a> </div><div class='result-snippet'>";
                h += html_escape(results[i].snippet);
                h += "</div><div class='result-score'> Score: ";
                h += to_string(results[i].score);
                h += "</div></div>";
            }
            h += " </div> ";
        }

        h += "</body> </html>";
        return h;
    }
};

static SearchPlugin theSearchPlugin;
