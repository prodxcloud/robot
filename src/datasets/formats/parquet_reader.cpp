#include <fstream>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>
#include "common/types.hpp"

#ifdef PRODXCLOUD_ARROW_ENABLED
#include <arrow/api.h>
#include <arrow/io/file.h>
#include <parquet/arrow/reader.h>
#endif

namespace prodxcloud::datasets {

class ParquetReader {
public:
    Result<Dataset> read_file(const std::string& path) const {
#ifdef PRODXCLOUD_ARROW_ENABLED
        auto mf = arrow::io::ReadableFile::Open(path);
        if (!mf.ok()) return std::unexpected(Error::not_found("Cannot open: " + path));
        std::unique_ptr<parquet::arrow::FileReader> reader;
        auto st = parquet::arrow::OpenFile(*mf, arrow::default_memory_pool(), &reader);
        if (!st.ok()) return std::unexpected(Error::internal("Parquet read error"));
        std::shared_ptr<arrow::Table> table;
        st = reader->ReadTable(&table);
        if (!st.ok()) return std::unexpected(Error::internal("Table read error"));
        Dataset ds; ds.name = path;
        for (int i = 0; i < table->num_columns(); ++i) ds.column_names.push_back(table->column(i)->name());
        for (int64_t r = 0; r < table->num_rows(); ++r) {
            DatasetRow row;
            for (int c = 0; c < table->num_columns(); ++c)
                row.fields[ds.column_names[c]] = table->column(c)->chunk(0)->ToString();
            ds.rows.push_back(std::move(row));
        }
        spdlog::info("Read Parquet: {} ({} rows)", path, ds.rows.size());
        return ds;
#else
        return std::unexpected(Error::internal("Parquet support not available"));
#endif
    }
};

}  // namespace prodxcloud::datasets
