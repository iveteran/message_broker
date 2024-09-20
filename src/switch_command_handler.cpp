#include <sstream>
#include "switch_command_handler.h"
#include "switch_server.h"
#include "command_messages.h"

#define _DECODE_COMMAND_MESSAGE(func_name, cmd_msg, cmd_obj, conn) { \
    if (cmd_msg->payload_len > 0) { \
        string payload(cmd_msg->payload, cmd_msg->payload_len); \
        if (cmd_msg->IsJSON()) { \
            cmd_obj.decodeFromJSON(payload); \
        } else if (cmd_msg->IsPB()) { \
            cmd_obj.decodeFromPB(payload); \
        } else { \
            int8_t errcode = 1; \
            const char* errmsg = "Unsupported codec of message payload"; \
            fprintf(stderr, "[%s] Error: %s\n", func_name, errmsg); \
            sendResultMessage(conn, cmd, errcode, errmsg); \
            return errcode; \
        } \
    } \
}

#define _CHECK_ROLE_PERMISSION(func_name, role, expect_role) { \
    if (role != expect_role) { \
        int8_t errcode = 1; \
        std::stringstream ss; \
        ss << "the operation not allowed for role: " << EndpointRoleToTag(role); \
        string errmsg = ss.str(); \
        fprintf(stderr, "[%s] Error: %s\n", func_name, errmsg.c_str()); \
        \
        sendResultMessage(ep->Connection(), cmd, errcode, errmsg); \
        return errcode; \
    } \
}

int CommandHandler::handleEcho(TcpConnection* conn, const CommandMessage* cmdMsg, const string& data)
{
    const ECommand cmd = (ECommand)cmdMsg->cmd;
    int8_t errcode = 0;
    sendResultMessage(conn, cmd, errcode, cmdMsg->payload, cmdMsg->payload_len);

    return 0;
}

int CommandHandler::handleRegister(TcpConnection* conn, const CommandMessage* cmdMsg, const string& data)
{
    // 1) authorize by access token
    // 2) set endpoint role - endpoint, admin, proxy, rproxy, cluster node
    // 3) set endpoint id

    const ECommand cmd = (ECommand)cmdMsg->cmd;

    CommandRegister reg_cmd;
    _DECODE_COMMAND_MESSAGE("handleRegister", cmdMsg, reg_cmd, conn);

    auto [errcode, errmsg] = service_->register_endpoint(conn, reg_cmd);
    if (! errmsg.empty()) {
        cerr << "[handleRegister] Error: " << errmsg << endl;
    }

    sendResultMessage(conn, cmd, errcode);

    return errcode;
}

int CommandHandler::handleForward(EndpointPtr ep, const CommandMessage* cmdMsg, const string& data)
{
    const ECommand cmd = (ECommand)cmdMsg->cmd;

    CommandForward cmd_fwd;
    _DECODE_COMMAND_MESSAGE("handleForward", cmdMsg, cmd_fwd, ep->Connection());

    auto [errcode, errmsg] = service_->forward(ep.get(), cmd_fwd);
    if (! errmsg.empty()) {
        cerr << "[handleForward] Error: " << errmsg << endl;
    }

    sendResultMessage(ep->Connection(), cmd, errcode);

    return errcode;
}

int CommandHandler::handleData(EndpointPtr ep, const CommandMessage* cmdMsg, const string& data)
{
    const ECommand cmd = (ECommand)cmdMsg->cmd;
    reverseToNetworkMessage((CommandMessage*)cmdMsg);

    auto fwd_targets = ep->GetForwardTargets();
    if (fwd_targets.empty() || fwd_targets[0] == 0) {
        // broadcast
        for (auto iter : context_->endpoints) {
            EndpointId ep_id = iter.first;
            auto target_ep = iter.second;
            if (ep_id == ep->Id()) {
                // It's self
                continue;
            }
            printf("[handleData] forward message: size: %ld\n", data.size());
            target_ep->Connection()->Send(data);
        }
    } else {
        // multicast
        for (auto ep_id : fwd_targets) {
            auto iter = context_->endpoints.find(ep_id);
            if (iter == context_->endpoints.end()) {
                continue;
            }
            auto target_ep = iter->second;
            printf("[handleData] forward message, size: %ld\n", data.size());
            target_ep->Connection()->Send(data);
        }
    }

    int8_t errcode = 0;
    sendResultMessage(ep->Connection(), cmd, errcode);

    return 0;
}

int CommandHandler::handleInfo(EndpointPtr ep, const CommandMessage* cmdMsg, const string& data)
{
    // 1) endpoints number -> current, connected and left of total and per endpoint
    // 2) tx/rx bytes -> total, per endpoint
    // 3) uptime
    // 4) pending connections, rejected by timeout of unregister
    // 5) command stats
    // *) other context info

    const ECommand cmd = (ECommand)cmdMsg->cmd;

    _CHECK_ROLE_PERMISSION("handleInfo", ep->GetRole(), EEndpointRole::Admin);

    CommandInfoReq cmd_info_req;
    _DECODE_COMMAND_MESSAGE("handleInfo", cmdMsg, cmd_info_req, ep->Connection());

    auto cmd_info = service_->get_stats(cmd_info_req);
    string rsp_data = cmd_info->encodeToJSON();
    int8_t errcode = 0;
    sendResultMessage(ep->Connection(), cmd, errcode, rsp_data);

    return 0;
}

int CommandHandler::handleSetup(EndpointPtr ep, const CommandMessage* cmdMsg, const string& data)
{
    // 0) authorize admin token
    // 1) set/generate admin token
    // 2) set/generate endpoint access token
    // 3) -- set serving mode

    const ECommand cmd = (ECommand)cmdMsg->cmd;

    _CHECK_ROLE_PERMISSION("handleSetup", ep->GetRole(), EEndpointRole::Admin);

    CommandSetup cmd_setup;
    _DECODE_COMMAND_MESSAGE("handleSetup", cmdMsg, cmd_setup, ep->Connection());

    auto [errcode, errmsg] = service_->setup(cmd_setup);
    if (! errmsg.empty()) {
        cerr << "[handleSetup] Error: " << errmsg << endl;
    }

    sendResultMessage(ep->Connection(), cmd, errcode, errmsg);

    return 0;
}

int CommandHandler::handleProxy(EndpointPtr ep, const CommandMessage* cmdMsg, const string& data)
{
    return 0;
}

int CommandHandler::handleKickout(EndpointPtr ep, const CommandMessage* cmdMsg, const string& data)
{
    // 1) kickout endpoint(s)

    const ECommand cmd = (ECommand)cmdMsg->cmd;

    _CHECK_ROLE_PERMISSION("handleKickout", ep->GetRole(), EEndpointRole::Admin);

    CommandKickout cmd_kickout;
    _DECODE_COMMAND_MESSAGE("handleKickout", cmdMsg, cmd_kickout, ep->Connection());

    auto [errcode, errmsg] = service_->kickout_endpoint(cmd_kickout);
    if (! errmsg.empty()) {
        cerr << "[handleKickout] Error: " << errmsg << endl;
    }

    sendResultMessage(ep->Connection(), cmd, errcode, errmsg);

    return 0;
}

int CommandHandler::handleReload(EndpointPtr ep, const CommandMessage* cmdMsg, const string& data)
{
    const ECommand cmd = (ECommand)cmdMsg->cmd;

    if (ep->GetRole() != EEndpointRole::Admin) {
        int8_t errcode = 1;
        std::stringstream ss;
        ss << "Failed: Operation not allowed for role: " << EndpointRoleToTag(ep->GetRole());
        string errmsg = ss.str();
        cerr << "[handleReload] Error: " << errmsg << endl;
        sendResultMessage(ep->Connection(), cmd, errcode, errmsg);
        return errcode;
    }

    return 0;
}

size_t CommandHandler::sendResultMessage(TcpConnection* conn, ECommand cmd, int8_t errcode, const string& data)
{
    return sendResultMessage(conn, cmd, errcode, data.data(), data.size());
}

size_t CommandHandler::sendResultMessage(TcpConnection* conn, ECommand cmd, int8_t errcode,
        const char* data, size_t data_len)
{
    if (data) {
        cout << "[sendResultMessage] response: " << data << endl;
    }

    size_t sent_bytes = 0;

    CommandMessage cmdMsg;
    cmdMsg.cmd = (uint8_t)cmd;
    cmdMsg.SetResponseFlag();
    cmdMsg.SetToJSON();
    uint32_t payload_len = sizeof(ResultMessage) + data_len;
    if (context_->switch_server->IsMessagePayloadLengthIncludingSelf()) {
        payload_len += sizeof(CommandMessage::payload_len);
    }
    cmdMsg.payload_len = htonl(payload_len);

    ResultMessage resultMsg;
    resultMsg.errcode = errcode;

    conn->Send((char*)&cmdMsg, sizeof(cmdMsg));
    sent_bytes += sizeof(cmdMsg);
    conn->Send((char*)&resultMsg, sizeof(resultMsg));
    sent_bytes += sizeof(resultMsg);
    if (data && data_len > 0) {
        conn->Send(data, data_len);
        sent_bytes += sizeof(data_len);
    }

    return sent_bytes;
}

// Reverse to network message without header of CommandMessage
std::pair<size_t, const char*>
    CommandHandler::extractMessagePayload(CommandMessage* cmdMsg)
{
    char* payload_ptr = cmdMsg->payload;
    size_t payload_len = cmdMsg->payload_len;
    if (context_->switch_server->IsMessagePayloadLengthIncludingSelf()) {
        payload_ptr -= sizeof(cmdMsg->payload_len);
        payload_len += sizeof(cmdMsg->payload_len);
    }
    cmdMsg->payload_len = htonl(payload_len);
    return std::make_pair(payload_len, payload_ptr);
}

// Reverse to network message with header of CommandMessage
Message* CommandHandler::reverseToNetworkMessage(CommandMessage* cmdMsg)
{
    if (context_->switch_server->IsMessagePayloadLengthIncludingSelf()) {
        cmdMsg->payload_len += sizeof(cmdMsg->payload_len);
    }
    cmdMsg->payload_len = htonl(cmdMsg->payload_len);
    auto msg = (Message*)cmdMsg;
    return msg;
}
