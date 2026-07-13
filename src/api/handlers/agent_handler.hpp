#pragma once
#include <memory>
#include <crow.h>
#include "agents/agent_controller.hpp"

namespace prodxcloud::api::handlers {

class AgentHandler {
public:
    explicit AgentHandler(std::shared_ptr<agents::AgentController> controller);

    crow::response list_agents(const crow::request& req);
    crow::response get_agent(const crow::request& req, const std::string& agent_id);
    crow::response create_agent(const crow::request& req);
    crow::response update_agent(const crow::request& req, const std::string& agent_id);
    crow::response execute_agent(const crow::request& req, const std::string& agent_id);
    crow::response delete_agent(const crow::request& req, const std::string& agent_id);
    crow::response chat(const crow::request& req, const std::string& agent_id);
    crow::response get_chat_history(const crow::request& req, const std::string& agent_id);

private:
    std::shared_ptr<agents::AgentController> controller_;
};

}  // namespace prodxcloud::api::handlers
