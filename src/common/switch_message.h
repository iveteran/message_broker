#ifndef _SWITCH_MESSAGE_H_
#define _SWITCH_MESSAGE_H_

#include "switch_types.h"
#include <cstdint>
#include <cstring>
#include <utility>
#include <cstddef>  // for offsetof
#include <string>

namespace evt_loop {
    class Message;
}
using evt_loop::Message;

using command_t = uint8_t;
using payload_size_t = uint16_t;

enum class ECommand : command_t {
    UNDEFINED,
    ECHO,
    REG,
    FWD,   // publish targets
    UNFWD,
    SUB,
    UNSUB,
    REJECT,
    UNREJECT,
    PUBLISH,    // publish data
    PUBLISH_2,  // publish data to targets
    SVC,   // service request
    INFO,
    EP_INFO,
    SETUP,
    PROXY,
    KICKOUT,
    EXIT,
    RELOAD,
    HEARTBEAT = 254,
    RESULT = 255,
};
const char* CommandToTag(ECommand cmd);

// for CommandMessage.cmd equals ECommand::SVC
#pragma pack(1)
struct ServiceMessage {
    svc_type_t svc_type = 0;   // service type, used for service routing, 0: all, others defined by Services
    msg_type_t svc_cmd  = 0;   // defined by Services
    uint32_t   sess_id  = 0;   // session id of request/response
    ep_id_t    source   = 0;   // requester id (endpoint id)
};
#pragma pack()

// for CommandMessage.cmd equals ECommand::PUBLISH_2
#pragma pack(1)
struct PublishingMessage {
    msg_type_t msg_type  = 0;
    ep_id_t    source    = 0;     // source endpoint id
    uint16_t   n_targets = 0;     // number of targets
    ep_id_t    targets[0];        // more than one element
};
#pragma pack()

#pragma pack(1)
struct ResultMessage {
    int8_t errcode = 0;
    char data[0];       // placeholder field
};
#pragma pack()

#pragma pack(1)
struct CommandMessage {
    private:
    // Fields
    command_t cmd_ = 0;         // ECommand
    struct {
        uint8_t unused:5 = 0;
        uint8_t codec:2 = 0;    // 2 bits, codec of above layer, 0: undefined, 1: json, 2: protobuf, 3: unused
        uint8_t req_rsp:1 = 0;  // 1 bit,  request or response,  0: request, 1: response
    } flag_;
    payload_size_t payload_len_ = 0;    // payload_len, XXX: does not including self size
    char payload_[0];                   // placeholder field, ServiceMessage, PublishingMessage or payload

    public:
    // Member methods
    ECommand Command() const { return (ECommand)cmd_; }
    void SetCommand(ECommand cmd) { cmd_ = (command_t)cmd; }
    void SetCommand(command_t cmd) { cmd_ = cmd; }

    void SetResponseFlag() { flag_.req_rsp = 1; }
    bool HasResponseFlag() const { return flag_.req_rsp; }

    void SetToJSON() { flag_.codec = 1; }
    bool IsJSON() const { return flag_.codec == 1; }

    void SetToPB() { flag_.codec = 2; }
    bool IsPB() const { return flag_.codec == 2; }

    void ResetCodec() { flag_.codec = 0; }

    void SetPayloadLen(payload_size_t length) { payload_len_ = length; }
    std::pair<const char*, payload_size_t> Payload() const;
    payload_size_t PayloadLen() const;

    const char* Data() const { return (const char*)this; }
    size_t Size() const {
        return HeaderSize() + payload_len_;
    }
    std::string CopyData() const {
        std::string data((char*)this, sizeof(*this));
        data.append(payload_, payload_len_);
        return data;
    }

    const PublishingMessage* GetPublishingMessage() const;
    const ServiceMessage* GetServiceMessage() const;
    const ResultMessage* GetResultMessage() const;
    size_t GetResultMessageContentSize() const;
    const char* GetResultMessageContent() const;
    Message* ConvertToNetworkMessage(bool isMsgPayloadLengthIncludingSelf);

    // Static methods
    static size_t HeaderSize() { return sizeof(CommandMessage); }
    static size_t PayloadLenBytes() { return sizeof(CommandMessage::payload_len_); }
    static size_t OffsetOfPayloadLen() { return offsetof(CommandMessage, payload_len_); }
    static CommandMessage* FromNetworkMessage(const Message* msg, bool isMsgPayloadLengthIncludingSelf);
    static CommandMessage CreateHeartbeatRequest();
    static CommandMessage CreateHeartbeatResponse();
};
#pragma pack()

#endif  // _SWITCH_MESSAGE_H_
