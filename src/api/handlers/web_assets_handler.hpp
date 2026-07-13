#pragma once
#include <crow.h>

namespace prodxcloud::api::handlers {

class WebAssetsHandler {
public:
    crow::response list_web_assets(const crow::request& req);
    crow::response get_web_asset(const crow::request& req, const std::string& id);
    crow::response create_web_asset(const crow::request& req);
    crow::response delete_web_asset(const crow::request& req, const std::string& id);
};

}  // namespace prodxcloud::api::handlers
