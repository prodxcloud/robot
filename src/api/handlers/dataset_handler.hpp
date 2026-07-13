#pragma once
#include <crow.h>

namespace prodxcloud::api::handlers {

class DatasetHandler {
public:
    // Existing
    crow::response validate(const crow::request& req);
    crow::response clean(const crow::request& req);

    // CRUD
    crow::response list_datasets(const crow::request& req);
    crow::response get_dataset(const crow::request& req, const std::string& id);
    crow::response create_dataset(const crow::request& req);
    crow::response delete_dataset(const crow::request& req, const std::string& id);
};

}  // namespace prodxcloud::api::handlers
