#ifndef _VERTC_PROTOCOL_H_
#define _VERTC_PROTOCOL_H_


#include "protocol.h"

#include <web_socket.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include "VolcEngineRTCLite.h"

#define VERTC_PROTOCOL_SERVER_HELLO_EVENT (1 << 0)

// 应用ID
static const std::string BYTE_RTC_APP_ID("67cd0c8fc673e001a2175ef2");
// SDK客户端用户ID
static const std::string BYTE_RTC_UID("8888");
// 房间ID
static const std::string BYTE_RTC_ROOM_ID("1234");
// TOKEN
static const std::string BYTE_RTC_TOKEN("");
// 远端智能体ID
static const std::string BYTE_RTC_REMOTE_ID("voiceChat_9999");

typedef struct {
    char room_id[129];
    char uid[129];
    char app_id[25];
    char token[257];
} rtc_room_info_t;

typedef struct {
    //player_pipeline_handle_t player_pipeline;
    // 播放管道进行替换成小智方案的音频回调
    std::function<void(std::vector<uint8_t>&& data)> on_incoming_audio;
    rtc_room_info_t* room_info;
    // 远端智能体ID
    char remote_uid[128];
} engine_context_t;


class VeRtcProtocol : public Protocol {
public:
    VeRtcProtocol();
    ~VeRtcProtocol();

    void Start() override;
    void InitRoomInfo();
    void SendAudio(const std::vector<uint8_t>& data) override;
    bool OpenAudioChannel() override;
    void CloseAudioChannel() override;
    bool IsAudioChannelOpened() const override;

    virtual void OnIncomingAudio(std::function<void(std::vector<uint8_t>&& data)> callback);

private:
    EventGroupHandle_t event_group_handle_;
    WebSocket* websocket_ = nullptr;

    byte_rtc_engine_t engine = nullptr;
    rtc_room_info_t *roomInfo = nullptr;
    
    void ParseServerHello(const cJSON* root);
    void SendText(const std::string& text) override;
};

#endif
