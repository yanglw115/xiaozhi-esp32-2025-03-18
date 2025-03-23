#include "vertc_protocol.h"
#include "board.h"
#include "system_info.h"
#include "application.h"

#include <cstring>
#include <cJSON.h>
#include <esp_log.h>
#include <arpa/inet.h>
#include "assets/lang_config.h"

#define TAG "VERTC"

static bool joined = false;

static std::function<void(std::vector<uint8_t>&& data)> g_on_incoming_audio = nullptr;

// byte rtc lite callbacks
static void byte_rtc_on_join_room_success(byte_rtc_engine_t engine, const char* channel, int elapsed_ms, bool something) {
    ESP_LOGI(TAG, "join channel success %s elapsed %d ms now %d ms\n", channel, elapsed_ms, elapsed_ms);
    joined = true;
};

static void byte_rtc_on_rejoin_room_success(byte_rtc_engine_t engine, const char* channel, int elapsed_ms){
    // g_byte_rtc_data.channel_joined = TRUE;
    ESP_LOGI(TAG, "rejoin channel success %s\n", channel);
};

static void byte_rtc_on_user_joined(byte_rtc_engine_t engine, const char* channel, const char* user_name, int elapsed_ms){
    ESP_LOGI(TAG, "remote user joined  %s:%s\n", channel ? channel : "", user_name ? user_name : "");
    engine_context_t* context = (engine_context_t *) byte_rtc_get_user_data(engine);
    strcpy(context->remote_uid, user_name);
};

static void byte_rtc_on_user_offline(byte_rtc_engine_t engine, const char* channel, const char* user_name, int reason){
    ESP_LOGI(TAG, "remote user offline  %s:%s\n", channel ? channel : "", user_name ? user_name : "");
};

static void byte_rtc_on_user_mute_audio(byte_rtc_engine_t engine, const char* channel, const char* user_name, int muted){
    ESP_LOGI(TAG, "remote user mute audio  %s:%s %d\n", channel ? channel : "", user_name ? user_name : "", muted);
};

static void byte_rtc_on_user_mute_video(byte_rtc_engine_t engine, const char* channel, const char* user_name, int muted){
    ESP_LOGI(TAG, "remote user mute video  %s:%s %d\n", channel ? channel : "", user_name ? user_name : "", muted);
};

static void byte_rtc_on_connection_lost(byte_rtc_engine_t engine, const char* channel){
    ESP_LOGI(TAG, "connection Lost  %s\n", channel ? channel : "");
};

static void byte_rtc_on_room_error(byte_rtc_engine_t engine, const char* channel, int code, const char* msg){
    ESP_LOGE(TAG, "on room error: error occur %s %d %s\n", channel, code, msg?msg:"");
};

// remote audio
static void byte_rtc_on_audio_data(byte_rtc_engine_t engine, const char* channel, const char*  uid , uint16_t sent_ts,
                      audio_codec_type_e codec, const void* data_ptr, size_t data_len){
    ESP_LOGI(TAG, "byte_rtc_on_audio_data... len %d\n", data_len);
    ESP_LOGI(TAG, "byte_rtc_on_audio_data... codec type %d\n", codec);

    // engine_context_t* context = (engine_context_t *) byte_rtc_get_user_data(engine);
    //player_pipeline_write(context->player_pipeline, data_ptr, data_len);
    // std::vector<uint8_t> data((uint8_t*)data_ptr, (uint8_t*)data_ptr + data_len);
    // if (g_on_incoming_audio != nullptr && data_ptr != nullptr) {
    //     g_on_incoming_audio(std::move(data));
    // }

}

// remote video
static void byte_rtc_on_video_data(byte_rtc_engine_t engine, const char*  channel, const char* uid, uint16_t sent_ts,
                      video_data_type_e codec, int is_key_frame,
                      const void * data_ptr, size_t data_len){
    ESP_LOGI(TAG, "byte_rtc_on_video_data... len %d\n", data_len);
}

VeRtcProtocol::VeRtcProtocol() {
    event_group_handle_ = xEventGroupCreate();
    on_incoming_audio_ = nullptr;
}

VeRtcProtocol::~VeRtcProtocol() {
    if (websocket_ != nullptr) {
        delete websocket_;
    }
    vEventGroupDelete(event_group_handle_);
}

// 提示流发布端需重新生成关键帧的回调
static void on_key_frame_gen_req(byte_rtc_engine_t engine, const char*  channel, const char*  uid)
{

}

// remote message
// 字幕消息 参考https://www.volcengine.com/docs/6348/1337284
static void on_subtitle_message_received(byte_rtc_engine_t engine, const cJSON* root) {
    /*
        {
            "data" : 
            [
                {
                    "definite" : false,
                    "language" : "zh",
                    "mode" : 1,
                    "paragraph" : false,
                    "sequence" : 0,
                    "text" : "\\u4f60\\u597d",
                    "userId" : "voiceChat_xxxxx"
                }
            ],
            "type" : "subtitle"
        }
    */
    cJSON * type_obj = cJSON_GetObjectItem(root, "type");
    if (type_obj != NULL && strcmp("subtitle", cJSON_GetStringValue(type_obj)) == 0) {
        cJSON* data_obj_arr = cJSON_GetObjectItem(root, "data");
        cJSON* obji = NULL;
        cJSON_ArrayForEach(obji, data_obj_arr) {
            cJSON* user_id_obj = cJSON_GetObjectItem(obji, "userId");
            cJSON* text_obj = cJSON_GetObjectItem(obji, "text");
            if (user_id_obj && text_obj) {
                ESP_LOGE(TAG, "subtitle:%s:%s", cJSON_GetStringValue(user_id_obj), cJSON_GetStringValue(text_obj));
            }
        }
    }
}

// function calling 消息 参考 https://www.volcengine.com/docs/6348/1359441
static void on_function_calling_message_received(byte_rtc_engine_t engine, const cJSON* root, const char* json_str) {
    /*
        {
            "subscriber_user_id" : "",
            "tool_calls" : 
            [
                {
                    "function" : 
                    {
                        "arguments" : "{\\"location\\": \\"\\u5317\\u4eac\\u5e02\\"}",
                        "name" : "get_current_weather"
                    },
                    "id" : "call_py400kek0e3pczrqdxgnb3lo",
                    "type" : "function"
                }
            ]
        }
    */
    // 收到function calling 消息，需要根据具体情况要在服务端处理还是客户端处理

    engine_context_t* context = (engine_context_t *) byte_rtc_get_user_data(engine);
    
    // 服务端处理：
    // voice_bot_function_calling(context->room_info, json_str);

    // 在客户端处理,通过byte_rtc_rts_send_message接口通知智能体
    /*cJSON* tool_obj_arr = cJSON_GetObjectItem(root, "tool_calls");
    cJSON* obji = NULL;
    cJSON_ArrayForEach(obji, tool_obj_arr) {
        cJSON* id_obj = cJSON_GetObjectItem(obji, "id");
        cJSON* function_obj = cJSON_GetObjectItem(obji, "function");
        if (id_obj && function_obj) {
            cJSON* arguments_obj = cJSON_GetObjectItem(function_obj, "arguments");
            cJSON* name_obj = cJSON_GetObjectItem(function_obj, "name");
            cJSON* location_obj = cJSON_GetObjectItem(arguments_obj, "arguments");
            const char* func_name = cJSON_GetStringValue(name_obj);
            const char* loction = cJSON_GetStringValue(location_obj);
            const char* func_id = cJSON_GetStringValue(id_obj);

            if (strcmp(func_name, "get_current_weather") == 0) {
                cJSON *fc_obj = cJSON_CreateObject();
                cJSON_AddStringToObject(fc_obj, "ToolCallID", func_id);
                cJSON_AddStringToObject(fc_obj, "Content", "今天白天风和日丽，天气晴朗，晚上阵风二级。");
                char *json_string = cJSON_Print(fc_obj);
                static char fc_message_buffer[256] = {'f', 'u', 'n', 'c'};
                int json_str_len = strlen(json_string);
                fc_message_buffer[4] = (json_str_len >> 24) & 0xff;
                fc_message_buffer[5] = (json_str_len >> 16) & 0xff;
                fc_message_buffer[6] = (json_str_len >> 8) & 0xff;
                fc_message_buffer[7] = (json_str_len >> 0) & 0xff;
                memcpy(fc_message_buffer + 8, json_string, json_str_len);
                ESP_LOGE(TAG, "send message: %s", json_string);
                cJSON_Delete(fc_obj);

                byte_rtc_rts_send_message(engine, context->room_info->room_id, context->remote_uid, fc_message_buffer, json_str_len + 8, 1, RTS_MESSAGE_RELIABLE);
            }
        }
    }*/
   
}

void on_message_received(byte_rtc_engine_t engine, const char*  room, const char* uid, const uint8_t* message, int size, bool binary) {
    // 字幕消息，参考https://www.volcengine.com/docs/6348/1337284
    // subv|length(4)|json str
    //
    // function calling 消息，参考https://www.volcengine.com/docs/6348/1359441
    // tool|length(4)|json str

    static char message_buffer[4096];
    if (size > 8) {
        memcpy(message_buffer, message, size);
        message_buffer[size] = 0;
        message_buffer[size + 1] = 0;
        cJSON *root = cJSON_Parse(message_buffer + 8);
        if (root != NULL) {
            if (message[0] == 's' && message[1] == 'u' && message[2] == 'b' && message[3] == 'v') {
                // 字幕消息
                on_subtitle_message_received(engine, root);
            } else if (message[0] == 't' && message[1] == 'o' && message[2] == 'o' && message[3] == 'l') {
                // function calling 消息
                on_function_calling_message_received(engine, root, message_buffer + 8);
            } else {
                ESP_LOGE(TAG, "unknown json message: %s", message_buffer + 8);
            }
            cJSON_Delete(root);
        } else {
            ESP_LOGE(TAG, "unknown message.");
        }
    } else {
        ESP_LOGE(TAG, "unknown message.");
    }
}

// SDK 错误信息回调
void byte_rtc_on_global_error(byte_rtc_engine_t engine, int code, const char * msg) 
{
    ESP_LOGE(TAG, "code(%d), message(%s).", code, msg ? msg : "");
}

void byte_rtc_on_target_bitrate_changed(byte_rtc_engine_t engine,const char * room, uint32_t target_bps)
{

}

// 实时信令消息发送结果通知
void byte_rtc_on_message_send_result(byte_rtc_engine_t engine,const char * room,int64_t msgid, int error,const char * extencontent)
{
    ESP_LOGE(TAG, "error(%d), extencontent(%s).", error, extencontent);
}

// Token 加入房间权限过期前 30 秒，触发该回调
void byte_rtc_on_token_privilege_will_expire(byte_rtc_engine_t engine,const char * room)
{
    ESP_LOGE(TAG, "room(%s).", room);
}

// license 过期提醒。在剩余天数低于 30 天时，收到此回调
void byte_rtc_on_license_expire_warning(byte_rtc_engine_t engine,int daysleft)
{
    ESP_LOGW(TAG, "days left(%d).", daysleft);
}

// engine 实例清理(byte_rtc_fini)结束通知，只有收到该通知之后，重新创建实例(byte_rtc_init)才是安全的
void byte_rtc_on_fini_notify(byte_rtc_engine_t engine)
{
    ESP_LOGI(TAG, "");
}

void VeRtcProtocol::InitRoomInfo()
{
    strcpy(roomInfo->app_id, BYTE_RTC_APP_ID.c_str());
    strcpy(roomInfo->room_id, BYTE_RTC_ROOM_ID.c_str());
    strcpy(roomInfo->uid, BYTE_RTC_UID.c_str());
    strcpy(roomInfo->token, BYTE_RTC_TOKEN.c_str());
}

void VeRtcProtocol::Start() {
    roomInfo = (rtc_room_info_t*)heap_caps_malloc(sizeof(rtc_room_info_t),  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    // 创建引擎，加入房间
    byte_rtc_event_handler_t handler = {
        .on_global_error            =   byte_rtc_on_global_error,
        .on_join_room_success       =   byte_rtc_on_join_room_success,
        .on_room_error              =   byte_rtc_on_room_error,
        .on_user_joined             =   byte_rtc_on_user_joined,
        .on_user_offline            =   byte_rtc_on_user_offline,
        .on_user_mute_audio         =   byte_rtc_on_user_mute_audio,
        .on_user_mute_video         =   byte_rtc_on_user_mute_video,
        .on_key_frame_gen_req       =   on_key_frame_gen_req,
        .on_audio_data              =   byte_rtc_on_audio_data,
        .on_video_data              =   byte_rtc_on_video_data,
        .on_target_bitrate_changed  =   byte_rtc_on_target_bitrate_changed,
        .on_message_received        =   on_message_received,
        .on_message_send_result     =   byte_rtc_on_message_send_result,
        .on_token_privilege_will_expire = byte_rtc_on_token_privilege_will_expire,
        .on_license_expire_warning  = byte_rtc_on_license_expire_warning,
        .on_fini_notify             = byte_rtc_on_fini_notify
    };

    // 初始化room信息，临时用token，不需要向服务器去请求，但是需要在官网启动智能体
    InitRoomInfo();
    // 创建引擎实例
    engine = byte_rtc_create(roomInfo->app_id, &handler);
    if (NULL == engine) {
        ESP_LOGE(TAG, "byte_rtc_create failed!");
        return;
    }
    byte_rtc_set_log_level(engine, BYTE_RTC_LOG_LEVEL_ERROR);
    byte_rtc_set_params(engine, "{\"debug\":{\"log_to_console\":1}}");    
    // byte_rtc_set_params(engine,"{\"rtc\":{\"thread\":{\"pinned_to_core\":1}}}");
    // byte_rtc_set_params(engine,"{\"rtc\":{\"thread\":{\"priority\":5}}}");
 

    // 初始化引擎实例，只能初始化一次
    // 0：成功 <br>
    // -1: appid 或 event_handler 为空 <br>
    // -2：引擎实例已被初始化 <br>
    // -3：引擎实例创建失败，请检查是否有可用内存
    int iInit = byte_rtc_init(engine);
    switch (iInit) {
        case 0: 
            ESP_LOGI(TAG, "byte_rtc_init successful!");
            break;
        case -1:
            ESP_LOGE(TAG, "byte_rtc_init failed! appid or event_handler is null!");
            return;
            break;
        case -2:
            ESP_LOGW(TAG, "byte_rtc_init has been inited!");
            return;
            break;
        case -3:
            ESP_LOGE(TAG, "byte_rtc_init failed! Engine instance create failed, please check the memory is enough!");
            return;
            break;
        default:
            ESP_LOGW(TAG, "byte_rtc_init returned invalid value(%d)!", iInit);
            break;
    }
    byte_rtc_set_audio_codec(engine, AUDIO_CODEC_TYPE_OPUS);
    // byte_rtc_set_video_codec(engine, VIDEO_CODEC_TYPE_H264);

    // 设置上下文，便于在回调中获取上下文中的内容
    engine_context_t engine_context = {
        //.player_pipeline = player_pipeline,
        // .on_incoming_audio = this->on_incoming_audio_,
        .room_info = roomInfo,
    };
    // 将自定义的数据与引擎实例关联起来
    byte_rtc_set_user_data(engine, &engine_context);

    byte_rtc_room_options_t options;
    options.auto_subscribe_audio = 1; // 接收远端音频
    options.auto_subscribe_video = 0; // 不接收远端视频
    // 加入房间
    int iJoinRoom = byte_rtc_join_room(engine, roomInfo->room_id, roomInfo->uid, roomInfo->token, &options);
    // 0：成功 <br>
    // 1：引擎实例不存在 <br>
    // -2：输入参数为空 <br>
    // -3：已加入过房间
    switch (iJoinRoom)
    {
    case 0:
        ESP_LOGI(TAG, "byte_rtc_join_room successful!");
        break;
    case -1:
        ESP_LOGE(TAG, "byte_rtc_join_room failed! The engine instance is not exist!");
        break;
    case -2:
        ESP_LOGE(TAG, "byte_rtc_join_room failed! Input param is null!");
        break;
    case -3:
        ESP_LOGE(TAG, "byte_rtc_join_room: has joined room!");
        break;
    default:
        ESP_LOGW(TAG, "byte_rtc_join_room: invalid return value(%d)", iJoinRoom);
        break;
    }

    return;
    // DOTO: 目前没有条件触发离开房间，暂时不需要离开
    // 离开房间，销毁引擎
    // 0：成功 <br>
    // -1：引擎实例不存在 <br>
    // -2：输入参数为空
    int iLeave = byte_rtc_leave_room(engine, roomInfo->room_id);
    switch (iLeave)
    {
    case 0:
        ESP_LOGI(TAG, "byte_rtc_leave_room successful!");
        break;
    case -1:
        ESP_LOGE(TAG, "byte_rtc_leave_room failed! The engine instance is not exist!");
        break;
    case -2:
        ESP_LOGE(TAG, "byte_rtc_leave_room failed! Input param is null!");
        break;
    default:
        ESP_LOGW(TAG, "byte_rtc_leave_room: invalid return value(%d)", iLeave);
        break;
    }
    usleep(1000 * 1000);
    // 销毁引擎实例
    int iFinish = byte_rtc_fini(engine);
    switch (iFinish)
    {
    case 0:
        ESP_LOGI(TAG, "byte_rtc_fini successful!");
        break;
    case -1:
        ESP_LOGE(TAG, "byte_rtc_fini failed! The engine instance is not exist!");
        break;
    default:
        ESP_LOGW(TAG, "byte_rtc_fini: invalid return value(%d)", iLeave);
        break;
    }
    usleep(1000 * 1000);
    // 销毁引擎实例,只有在收到on_fini_notify的回调之后，调用此方法才是安全的
    byte_rtc_destory(engine);
    
    // 关闭智能体，如果不主动调用， 智能体会在远端离开后3分钟离开
    // TODO: 目前需要在网官web手动停止智能体
    heap_caps_free(roomInfo);

    // 关闭音频采播
    ESP_LOGI(TAG, "............. finished\n");
}

void VeRtcProtocol::SendAudio(const std::vector<uint8_t>& data) {
    // if (websocket_ == nullptr) {
    //     return;
    // }
    audio_frame_info_t audio_frame_info = {.data_type = AUDIO_DATA_TYPE_OPUS};
    int iSendAudio = byte_rtc_send_audio_data(engine, roomInfo->room_id, data.data(), data.size(), &audio_frame_info);
    // 0：成功 <br>
    // -1：引擎实例不存在 <br>
    // -2：输入参数为空
    switch (iSendAudio)
    {
    case 0:
        ESP_LOGI(TAG, "byte_rtc_send_audio_data successful, data size(%d)!", data.size());  
        break;
    case -1:
        ESP_LOGE(TAG, "byte_rtc_send_audio_data failed: The engine instance is not exist!");  
        break;   
    case -2:
        ESP_LOGE(TAG, "byte_rtc_send_audio_data failed: Input param is null!");  
        break; 
    default:
        ESP_LOGE(TAG, "byte_rtc_send_audio_data failed: Invaid return value(%d)!", iSendAudio);  
        break;
    }
    // websocket_->Send(data.data(), data.size(), true);
}

void VeRtcProtocol::SendText(const std::string& text) {
    // if (websocket_ == nullptr) {
    //     return;
    // }

    // if (!websocket_->Send(text)) {
    //     ESP_LOGE(TAG, "Failed to send text: %s", text.c_str());
    //     SetError(Lang::Strings::SERVER_ERROR);
    // }
}

bool VeRtcProtocol::IsAudioChannelOpened() const {
    // return websocket_ != nullptr && websocket_->IsConnected() && !error_occurred_ && !IsTimeout();
    return true; // TODO
}

void VeRtcProtocol::CloseAudioChannel() {
    if (websocket_ != nullptr) {
        delete websocket_;
        websocket_ = nullptr;
    }
}

bool VeRtcProtocol::OpenAudioChannel() {
    return true; // DOTO: 目前来看并不会用到websocket通讯

    if (websocket_ != nullptr) {
        delete websocket_;
    }

    error_occurred_ = false;
    std::string url = "";
    std::string token = "Bearer " + std::string("");
    websocket_ = Board::GetInstance().CreateWebSocket();
    websocket_->SetHeader("Authorization", token.c_str());
    websocket_->SetHeader("Protocol-Version", "1");
    websocket_->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    websocket_->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());

    websocket_->OnData([this](const char* data, size_t len, bool binary) {
        if (binary) {
            if (on_incoming_audio_ != nullptr) {
                on_incoming_audio_(std::vector<uint8_t>((uint8_t*)data, (uint8_t*)data + len));
            }
        } else {
            // Parse JSON data
            auto root = cJSON_Parse(data);
            auto type = cJSON_GetObjectItem(root, "type");
            if (type != NULL) {
                if (strcmp(type->valuestring, "hello") == 0) {
                    ParseServerHello(root);
                } else {
                    if (on_incoming_json_ != nullptr) {
                        on_incoming_json_(root);
                    }
                }
            } else {
                ESP_LOGE(TAG, "Missing message type, data: %s", data);
            }
            cJSON_Delete(root);
        }
        last_incoming_time_ = std::chrono::steady_clock::now();
    });

    websocket_->OnDisconnected([this]() {
        ESP_LOGI(TAG, "Websocket disconnected");
        if (on_audio_channel_closed_ != nullptr) {
            on_audio_channel_closed_();
        }
    });

    if (!websocket_->Connect(url.c_str())) {
        ESP_LOGE(TAG, "Failed to connect to websocket server");
        SetError(Lang::Strings::SERVER_NOT_FOUND);
        return false;
    }

    // Send hello message to describe the client
    // keys: message type, version, audio_params (format, sample_rate, channels)
    std::string message = "{";
    message += "\"type\":\"hello\",";
    message += "\"version\": 1,";
    message += "\"transport\":\"websocket\",";
    message += "\"audio_params\":{";
    message += "\"format\":\"opus\", \"sample_rate\":16000, \"channels\":1, \"frame_duration\":" + std::to_string(OPUS_FRAME_DURATION_MS);
    message += "}}";
    websocket_->Send(message);

    // Wait for server hello
    EventBits_t bits = xEventGroupWaitBits(event_group_handle_, VERTC_PROTOCOL_SERVER_HELLO_EVENT, pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
    if (!(bits & VERTC_PROTOCOL_SERVER_HELLO_EVENT)) {
        ESP_LOGE(TAG, "Failed to receive server hello");
        SetError(Lang::Strings::SERVER_TIMEOUT);
        return false;
    }

    if (on_audio_channel_opened_ != nullptr) {
        on_audio_channel_opened_();
    }

    return true;
}

void VeRtcProtocol::ParseServerHello(const cJSON* root) {
    return; // TODO: 不需要实现

    auto transport = cJSON_GetObjectItem(root, "transport");
    if (transport == nullptr || strcmp(transport->valuestring, "websocket") != 0) {
        ESP_LOGE(TAG, "Unsupported transport: %s", transport->valuestring);
        return;
    }

    auto audio_params = cJSON_GetObjectItem(root, "audio_params");
    if (audio_params != NULL) {
        auto sample_rate = cJSON_GetObjectItem(audio_params, "sample_rate");
        if (sample_rate != NULL) {
            server_sample_rate_ = sample_rate->valueint;
        }
    }

    xEventGroupSetBits(event_group_handle_, VERTC_PROTOCOL_SERVER_HELLO_EVENT);
}

void VeRtcProtocol::OnIncomingAudio(std::function<void(std::vector<uint8_t>&& data)> callback) {
    on_incoming_audio_ = callback;
    g_on_incoming_audio = callback;
}
