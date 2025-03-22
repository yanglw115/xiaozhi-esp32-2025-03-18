#ifndef _VERTC_PROTOCOL_H_
#define _VERTC_PROTOCOL_H_


#include "protocol.h"

#include <web_socket.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#define VERTC_PROTOCOL_SERVER_HELLO_EVENT (1 << 0)

class VeRtcProtocol : public Protocol {
public:
    VeRtcProtocol();
    ~VeRtcProtocol();

    void Start() override;
    void SendAudio(const std::vector<uint8_t>& data) override;
    bool OpenAudioChannel() override;
    void CloseAudioChannel() override;
    bool IsAudioChannelOpened() const override;

private:
    EventGroupHandle_t event_group_handle_;
    WebSocket* websocket_ = nullptr;

    void ParseServerHello(const cJSON* root);
    void SendText(const std::string& text) override;
};

#endif
