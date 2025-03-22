#include "vertc_protocol.h"
#include "board.h"
#include "system_info.h"
#include "application.h"

#include <cstring>
#include <cJSON.h>
#include <esp_log.h>
#include <arpa/inet.h>
#include "assets/lang_config.h"
#include "VolcEngineRTCLite.h"

#define TAG "VERTC"

static bool joined = false;

typedef struct {
    char room_id[129];
    char uid[129];
    char app_id[25];
    char token[257];
} rtc_room_info_t;

typedef struct {
    //player_pipeline_handle_t player_pipeline; // todo: 
    rtc_room_info_t* room_info;
    char remote_uid[128];
} engine_context_t;

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
    ESP_LOGI(TAG, "remote user joined  %s:%s\n", channel, user_name);
    engine_context_t* context = (engine_context_t *) byte_rtc_get_user_data(engine);
    strcpy(context->remote_uid, user_name);
};

static void byte_rtc_on_user_offline(byte_rtc_engine_t engine, const char* channel, const char* user_name, int reason){
    ESP_LOGI(TAG, "remote user offline  %s:%s\n", channel, user_name);
};

static void byte_rtc_on_user_mute_audio(byte_rtc_engine_t engine, const char* channel, const char* user_name, int muted){
    ESP_LOGI(TAG, "remote user mute audio  %s:%s %d\n", channel, user_name, muted);
};

static void byte_rtc_on_user_mute_video(byte_rtc_engine_t engine, const char* channel, const char* user_name, int muted){
    ESP_LOGI(TAG, "remote user mute video  %s:%s %d\n", channel, user_name, muted);
};

static void byte_rtc_on_connection_lost(byte_rtc_engine_t engine, const char* channel){
    ESP_LOGI(TAG, "connection Lost  %s\n", channel);
};

static void byte_rtc_on_room_error(byte_rtc_engine_t engine, const char* channel, int code, const char* msg){
    ESP_LOGE(TAG, "error occur %s %d %s\n", channel, code, msg?msg:"");
};

// remote audio
static void byte_rtc_on_audio_data(byte_rtc_engine_t engine, const char* channel, const char*  uid , uint16_t sent_ts,
                      audio_codec_type_e codec, const void* data_ptr, size_t data_len){
    // ESP_LOGI(TAG, "byte_rtc_on_audio_data... len %d\n", data_len);

    engine_context_t* context = (engine_context_t *) byte_rtc_get_user_data(engine);
    //player_pipeline_write(context->player_pipeline, data_ptr, data_len);

}

// remote video
static void byte_rtc_on_video_data(byte_rtc_engine_t engine, const char*  channel, const char* uid, uint16_t sent_ts,
                      video_data_type_e codec, int is_key_frame,
                      const void * data_ptr, size_t data_len){
    ESP_LOGI(TAG, "byte_rtc_on_video_data... len %d\n", data_len);
}

VeRtcProtocol::VeRtcProtocol() {
    event_group_handle_ = xEventGroupCreate();
}

VeRtcProtocol::~VeRtcProtocol() {
    if (websocket_ != nullptr) {
        delete websocket_;
    }
    vEventGroupDelete(event_group_handle_);
}

static void on_key_frame_gen_req(byte_rtc_engine_t engine, const char*  channel, const char*  uid) {}

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

void byte_rtc_on_global_error(byte_rtc_engine_t engine,int code, const char * msg) 
{

}

void byte_rtc_on_target_bitrate_changed(byte_rtc_engine_t engine,const char * room, uint32_t target_bps)
{

}

void byte_rtc_on_message_send_result(byte_rtc_engine_t engine,const char * room,int64_t msgid, int error,const char * extencontent)
{

}

void byte_rtc_on_token_privilege_will_expire(byte_rtc_engine_t engine,const char * room)
{

}

void byte_rtc_on_license_expire_warning(byte_rtc_engine_t engine,int daysleft)
{

}

void byte_rtc_on_fini_notify(byte_rtc_engine_t engine)
{

}

void VeRtcProtocol::Start() {
    rtc_room_info_t* room_info = (rtc_room_info_t*)heap_caps_malloc(sizeof(rtc_room_info_t),  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
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

    byte_rtc_engine_t engine = byte_rtc_create(room_info->app_id, &handler);
    byte_rtc_set_log_level(engine, BYTE_RTC_LOG_LEVEL_ERROR);
    byte_rtc_set_params(engine, "{\"debug\":{\"log_to_console\":1}}"); 

    byte_rtc_init(engine);
    byte_rtc_set_audio_codec(engine, AUDIO_CODEC_TYPE_G711A);
    byte_rtc_set_video_codec(engine, VIDEO_CODEC_TYPE_H264);

    // 设置上下文，便于在回调中获取上下文中的内容
    engine_context_t engine_context = {
        .player_pipeline = player_pipeline,
        .room_info = room_info
    };
    byte_rtc_set_user_data(engine, &engine_context);

    byte_rtc_room_options_t options;
    options.auto_subscribe_audio = 1; // 接收远端音频
    options.auto_subscribe_video = 0; // 不接收远端视频
    byte_rtc_join_room(engine, room_info->room_id, room_info->uid, room_info->token, &options);
    
    const int DEFAULT_READ_SIZE = recorder_pipeline_get_default_read_size(pipeline);
    uint8_t *audio_buffer = heap_caps_malloc(DEFAULT_READ_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!audio_buffer) {
        ESP_LOGE(TAG, "Failed to alloc audio buffer!");
        return;
    }

    // 发送音频数据，根据需要设置打断循环条件
    while (true) {
        int ret =  recorder_pipeline_read(pipeline, (char*) audio_buffer, DEFAULT_READ_SIZE);
        if (ret == DEFAULT_READ_SIZE && joined) {
            // push_audio data
            audio_frame_info_t audio_frame_info = {.data_type = AUDIO_DATA_TYPE_PCMA};
            byte_rtc_send_audio_data(engine, room_info->room_id, audio_buffer, DEFAULT_READ_SIZE, &audio_frame_info);
        }
    }

    // 离开房间，销毁引擎
    byte_rtc_leave_room(engine, room_info->room_id);
    usleep(1000 * 1000);
    byte_rtc_fini(engine);
    usleep(1000 * 1000);
    byte_rtc_destory(engine);
    
    // 关闭智能体，如果不主动调用， 智能体会在远端离开后3分钟离开
    stop_voice_bot(room_info);
    heap_caps_free(room_info);

    // 关闭音频采播
    recorder_pipeline_close(pipeline);
    player_pipeline_close(player_pipeline);
    ESP_LOGI(TAG, "............. finished\n");
}

void VeRtcProtocol::SendAudio(const std::vector<uint8_t>& data) {
    if (websocket_ == nullptr) {
        return;
    }

    websocket_->Send(data.data(), data.size(), true);
}

void VeRtcProtocol::SendText(const std::string& text) {
    if (websocket_ == nullptr) {
        return;
    }

    if (!websocket_->Send(text)) {
        ESP_LOGE(TAG, "Failed to send text: %s", text.c_str());
        SetError(Lang::Strings::SERVER_ERROR);
    }
}

bool VeRtcProtocol::IsAudioChannelOpened() const {
    return websocket_ != nullptr && websocket_->IsConnected() && !error_occurred_ && !IsTimeout();
}

void VeRtcProtocol::CloseAudioChannel() {
    if (websocket_ != nullptr) {
        delete websocket_;
        websocket_ = nullptr;
    }
}

bool VeRtcProtocol::OpenAudioChannel() {
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
