#pragma once
#include <crow.h>

namespace prodxcloud::api::handlers {

class KnowledgeHandler {
public:
    crow::response list_knowledge_bases(const crow::request& req);
    crow::response get_knowledge_base(const crow::request& req, const std::string& id);
    crow::response create_knowledge_base(const crow::request& req);
    crow::response delete_knowledge_base(const crow::request& req, const std::string& id);
    crow::response upload_documents(const crow::request& req, const std::string& id);
};

}  // namespace prodxcloud::api::handlers
