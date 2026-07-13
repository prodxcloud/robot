#include <charconv>
#include <regex>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>
#include "common/types.hpp"

namespace prodxcloud::datasets {

struct ValidationError { size_t row_index; std::string field_name; std::string message; };
struct FieldSchema { std::string name, type; bool required = false; std::string pattern; };

class SchemaValidator {
public:
    void add_field(FieldSchema f) { fields_.push_back(std::move(f)); }

    std::vector<ValidationError> validate(const Dataset& ds) const {
        std::vector<ValidationError> errors;
        for (size_t ri = 0; ri < ds.rows.size(); ++ri) {
            const auto& row = ds.rows[ri];
            for (const auto& fd : fields_) {
                const std::string* val = nullptr;
                auto it = row.fields.find(fd.name);
                if (it != row.fields.end()) val = &it->second;
                if (fd.required && (!val || val->empty())) {
                    errors.push_back({ri, fd.name, "Required field missing"}); continue;
                }
                if (!val || val->empty()) continue;
                if (!validate_type(*val, fd.type))
                    errors.push_back({ri, fd.name, "Type mismatch: expected " + fd.type});
                if (!fd.pattern.empty()) {
                    try { if (!std::regex_match(*val, std::regex(fd.pattern)))
                        errors.push_back({ri, fd.name, "Pattern mismatch"}); } catch (...) {}
                }
            }
        }
        spdlog::info("Schema validation: {} rows, {} errors", ds.rows.size(), errors.size());
        return errors;
    }

    static bool validate_type(const std::string& val, const std::string& type) {
        if (type == "string") return true;
        if (type == "int") { int r; auto [p, ec] = std::from_chars(val.data(), val.data()+val.size(), r); return ec == std::errc{} && p == val.data()+val.size(); }
        if (type == "float") { try { std::stof(val); return true; } catch (...) { return false; } }
        if (type == "bool") return val == "true" || val == "false" || val == "1" || val == "0";
        return true;
    }
private:
    std::vector<FieldSchema> fields_;
};

}  // namespace prodxcloud::datasets
