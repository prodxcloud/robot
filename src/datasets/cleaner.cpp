#include "datasets/cleaner.hpp"
#include <algorithm>
#include <spdlog/spdlog.h>

namespace prodxcloud::datasets {

DataCleaner& DataCleaner::add_stage(std::unique_ptr<CleaningStage> stage) {
    stages_.push_back(std::move(stage)); return *this;
}

namespace {
class LambdaStage : public CleaningStage {
public:
    LambdaStage(std::string n, std::function<Result<void>(Dataset&)> fn) : name_(std::move(n)), fn_(std::move(fn)) {}
    std::string name() const override { return name_; }
    size_t process(Dataset& ds) override { auto orig = ds.rows.size(); fn_(ds); return orig - ds.rows.size(); }
private:
    std::string name_;
    std::function<Result<void>(Dataset&)> fn_;
};
}

DataCleaner& DataCleaner::add_stage(const std::string& name, std::function<Result<void>(Dataset&)> fn) {
    stages_.push_back(std::make_unique<LambdaStage>(name, std::move(fn)));
    return *this;
}

Result<CleaningResult> DataCleaner::clean(Dataset& dataset) {
    auto start = Clock::now();
    CleaningResult result; result.rows_in = dataset.rows.size();
    for (auto& s : stages_) {
        size_t removed = s->process(dataset);
        result.rows_removed += removed; result.stage_names.push_back(s->name());
        spdlog::info("Stage '{}': removed {} rows", s->name(), removed);
    }
    result.rows_out    = dataset.rows.size();
    result.duration_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return result;
}

size_t RemoveEmptyRowsStage::process(Dataset& ds) {
    auto orig = ds.rows.size();
    ds.rows.erase(std::remove_if(ds.rows.begin(), ds.rows.end(), [](const DatasetRow& r) {
        return std::all_of(r.fields.begin(), r.fields.end(), [](const auto& f) { return f.second.empty(); });
    }), ds.rows.end());
    return orig - ds.rows.size();
}

size_t TrimWhitespaceStage::process(Dataset& ds) {
    for (auto& row : ds.rows)
        for (auto& [n, v] : row.fields) {
            auto s = v.find_first_not_of(" \t\n\r"), e = v.find_last_not_of(" \t\n\r");
            v = (s == std::string::npos) ? "" : v.substr(s, e - s + 1);
        }
    return 0;
}

RequiredFieldsStage::RequiredFieldsStage(std::vector<std::string> r) : required_(std::move(r)) {}

size_t RequiredFieldsStage::process(Dataset& ds) {
    auto orig = ds.rows.size();
    ds.rows.erase(std::remove_if(ds.rows.begin(), ds.rows.end(), [this](const DatasetRow& row) {
        for (const auto& req : required_) {
            auto it = row.fields.find(req);
            if (it == row.fields.end() || it->second.empty()) return true;
        }
        return false;
    }), ds.rows.end());
    return orig - ds.rows.size();
}

}  // namespace prodxcloud::datasets
