#pragma once
#include <crow.h>

namespace prodxcloud::api::handlers {

class CatalogHandler {
public:
    crow::response list_catalog(const crow::request& req);
    crow::response catalog_summary(const crow::request& req);
    crow::response get_catalog_item(const crow::request& req, const std::string& model_id);
};

}  // namespace prodxcloud::api::handlers
