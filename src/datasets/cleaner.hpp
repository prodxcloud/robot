#pragma once
/// @file cleaner.hpp
/// @brief Composable data cleaning pipeline with builder pattern.

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "common/types.hpp"

namespace prodxcloud::datasets {

struct CleaningResult {
    size_t rows_in = 0, rows_out = 0, rows_removed = 0;
    double duration_ms = 0.0;
    std::vector<std::string> stage_names;
};

class CleaningStage {
public:
    virtual ~CleaningStage() = default;
    virtual std::string name() const = 0;
    virtual size_t process(Dataset& dataset) = 0;
};

class DataCleaner {
public:
    DataCleaner& add_stage(std::unique_ptr<CleaningStage> stage);
    DataCleaner& add_stage(const std::string& name, std::function<Result<void>(Dataset&)> fn);
    Result<CleaningResult> clean(Dataset& dataset);
    [[nodiscard]] size_t stage_count() const { return stages_.size(); }
private:
    std::vector<std::unique_ptr<CleaningStage>> stages_;
};

class RemoveEmptyRowsStage : public CleaningStage {
public:
    std::string name() const override { return "remove_empty_rows"; }
    size_t process(Dataset& dataset) override;
};

class TrimWhitespaceStage : public CleaningStage {
public:
    std::string name() const override { return "trim_whitespace"; }
    size_t process(Dataset& dataset) override;
};

class RequiredFieldsStage : public CleaningStage {
public:
    explicit RequiredFieldsStage(std::vector<std::string> required);
    std::string name() const override { return "required_fields"; }
    size_t process(Dataset& dataset) override;
private:
    std::vector<std::string> required_;
};

}  // namespace prodxcloud::datasets
