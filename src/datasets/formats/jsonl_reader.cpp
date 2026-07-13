#include <fstream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "common/types.hpp"

namespace prodxcloud::datasets {

struct JsonlParseError { size_t line_number; std::string message, raw_line; };

class JSONLReader {
public:
    Result<Dataset> read_file(const std::string& path,
                               std::vector<JsonlParseError>* errors = nullptr) const {
        std::ifstream file(path);
        if (!file.is_open()) return std::unexpected(Error::not_found("JSONL not found: " + path));
        Dataset ds; ds.name = path;
        std::string line; size_t ln = 0, parsed = 0;
        while (std::getline(file, line)) {
            ++ln;
            if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
            try {
                auto j = nlohmann::json::parse(line);
                if (!j.is_object()) { if (errors) errors->push_back({ln, "Not an object", line}); continue; }
                DatasetRow row;
                for (auto& [k, v] : j.items()) {
                    row.fields[k] = v.is_string() ? v.get<std::string>() : v.dump();
                    if (parsed == 0) ds.column_names.push_back(k);
                }
                ds.rows.push_back(std::move(row)); ++parsed;
            } catch (const nlohmann::json::parse_error& e) {
                if (errors) errors->push_back({ln, e.what(), line.substr(0, 200)});
            }
        }
        spdlog::info("JSONL: {} ({} rows, {} errors)", path, ds.rows.size(), errors ? errors->size() : 0);
        return ds;
    }
};

}  // namespace prodxcloud::datasets
