#include "zello_protocol.h"

#include "board.h"

#include <arpa/inet.h>
#include <cJSON.h>
#include <cstring>
#include <esp_log.h>

#define TAG "Zello"

ZelloProtocol::ZelloProtocol(
    const std::string& username,
    const std::string& password,
    const std::string& auth_token,
    const std::string& channel)
    : username_(username),
      password_(password),
      auth_token_(auth_token),
      channel_(channel) {
}

ZelloProtocol::~ZelloProtocol() {
    if (websocket_) {
        websocket_->Close();
    }
}

bool ZelloProtocol::Start() {
    return OpenAudioChannel();
}

bool ZelloProtocol::OpenAudioChannel() {
    if (websocket_ && websocket_->IsConnected()) {
        return true;
    }

    auto network = Board::GetInstance().GetNetwork();

    websocket_ = network->CreateWebSocket(1);

    if (!websocket_) {
        ESP_LOGE(TAG, "Failed to create WebSocket");
        return false;
    }

    websocket_->OnData(
        [this](const char* data, size_t len, bool binary) {
            if (binary) {
                HandleBinary(data, len);
            } else {
                HandleJson(data, len);
            }
        }
    );

    websocket_->OnDisconnected([this]() {
        ESP_LOGW(TAG, "Disconnected");

        logged_in_ = false;
        channel_online_ = false;
        stream_active_ = false;
        stream_start_pending_ = false;
        stream_id_ = 0;

        if (on_audio_channel_closed_) {
            on_audio_channel_closed_();
        }
    });

    ESP_LOGI(TAG, "Connecting to Zello");

    if (!websocket_->Connect("wss://zello.io/ws")) {
        ESP_LOGE(TAG, "Unable to connect to Zello");
        return false;
    }

    /*
     * Zello Friends & Family logon.
     */
    cJSON* root = cJSON_CreateObject();

    const int seq = sequence_++;

    cJSON_AddStringToObject(root, "command", "logon");
    cJSON_AddNumberToObject(root, "seq", seq);
    cJSON_AddStringToObject(
        root,
        "auth_token",
        auth_token_.c_str()
    );

    cJSON_AddStringToObject(
        root,
        "username",
        username_.c_str()
    );

    cJSON_AddStringToObject(
        root,
        "password",
        password_.c_str()
    );

    cJSON* channels = cJSON_AddArrayToObject(
        root,
        "channels"
    );

    cJSON_AddItemToArray(
        channels,
        cJSON_CreateString(channel_.c_str())
    );

    char* json = cJSON_PrintUnformatted(root);

    bool result = websocket_->Send(json);

    cJSON_free(json);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Zello logon sent");

    return result;
}

bool ZelloProtocol::SendText(
    const std::string& text
) {
    if (!websocket_ ||
        !websocket_->IsConnected()) {
        return false;
    }

    return websocket_->Send(text);
}

void ZelloProtocol::SendStartListening(ListeningMode mode) {
    (void)mode;
}

void ZelloProtocol::SendStopListening() {
}

bool ZelloProtocol::StartStream() {
    if (!logged_in_ ||
        !channel_online_ ||
        stream_active_ ||
        stream_start_pending_) {

        ESP_LOGW(
            TAG,
            "Cannot start stream yet"
        );

        return false;
    }

    cJSON* root = cJSON_CreateObject();

    const int seq = sequence_++;

    cJSON_AddStringToObject(
        root,
        "command",
        "start_stream"
    );

    cJSON_AddNumberToObject(
        root,
        "seq",
        seq
    );

    cJSON_AddStringToObject(
        root,
        "channel",
        channel_.c_str()
    );

    cJSON_AddStringToObject(
        root,
        "type",
        "audio"
    );

    cJSON_AddStringToObject(
        root,
        "codec",
        "opus"
    );

    /*
     * 16000 Hz
     * 1 Opus frame per packet
     * 60 ms frame
     */
    cJSON_AddStringToObject(
        root,
        "codec_header",
        "gD4BPA=="
    );

    cJSON_AddNumberToObject(
        root,
        "packet_duration",
        60
    );

    char* json = cJSON_PrintUnformatted(root);

    bool result = websocket_->Send(json);

    cJSON_free(json);
    cJSON_Delete(root);

    if (result) {
        stream_start_pending_ = true;

        ESP_LOGI(
            TAG,
            "start_stream sent"
        );
    }

    return result;
}

bool ZelloProtocol::SendAudio(
    std::unique_ptr<AudioStreamPacket> packet
) {
    if (!websocket_ ||
        !websocket_->IsConnected() ||
        !stream_active_ ||
        stream_id_ == 0 ||
        !packet) {

        return false;
    }

    /*
     * Zello binary audio packet:
     *
     * byte 0      = 0x01
     * bytes 1-4   = stream_id, network byte order
     * bytes 5-8   = packet_id, network byte order
     * bytes 9...  = Opus payload
     */

    std::string output;

    output.resize(
        9 + packet->payload.size()
    );

    output[0] = 0x01;

    uint32_t sid = htonl(stream_id_);

    memcpy(
        &output[1],
        &sid,
        sizeof(sid)
    );

    /*
     * Zello ignores packet_id for
     * outgoing audio.
     */
    uint32_t packet_id = 0;

    memcpy(
        &output[5],
        &packet_id,
        sizeof(packet_id)
    );

    memcpy(
        &output[9],
        packet->payload.data(),
        packet->payload.size()
    );

    return websocket_->Send(
        output.data(),
        output.size(),
        true
    );
}

void ZelloProtocol::StopStream() {
    if (!stream_active_ ||
        stream_id_ == 0) {
        return;
    }

    cJSON* root = cJSON_CreateObject();

    cJSON_AddStringToObject(
        root,
        "command",
        "stop_stream"
    );

    cJSON_AddNumberToObject(
        root,
        "seq",
        sequence_++
    );

    cJSON_AddNumberToObject(
        root,
        "stream_id",
        stream_id_
    );

    cJSON_AddStringToObject(
        root,
        "channel",
        channel_.c_str()
    );

    char* json = cJSON_PrintUnformatted(root);

    websocket_->Send(json);

    cJSON_free(json);
    cJSON_Delete(root);

    ESP_LOGI(
        TAG,
        "stop_stream sent"
    );

    stream_active_ = false;
    stream_start_pending_ = false;
    stream_id_ = 0;
}

void ZelloProtocol::CloseAudioChannel(
    bool send_goodbye
) {
    if (stream_active_) {
        StopStream();
    }

    if (websocket_) {
        websocket_->Close();
    }

    logged_in_ = false;
    channel_online_ = false;
}

bool ZelloProtocol::IsAudioChannelOpened() const {
    return websocket_ &&
           websocket_->IsConnected() &&
           logged_in_;
}

void ZelloProtocol::HandleJson(
    const char* data,
    size_t len
) {
    cJSON* root =
        cJSON_ParseWithLength(data, len);

    if (!root) {
        ESP_LOGE(TAG, "Invalid JSON");
        return;
    }

    cJSON* command =
        cJSON_GetObjectItem(root, "command");

    cJSON* success =
        cJSON_GetObjectItem(root, "success");

    cJSON* error =
        cJSON_GetObjectItem(root, "error");

    /*
     * Logon response.
     */
    if (success &&
        cJSON_IsTrue(success) &&
        !logged_in_) {

        logged_in_ = true;

        ESP_LOGI(
            TAG,
            "Logged into Zello"
        );
    }

    /*
     * Channel status.
     */
    if (cJSON_IsString(command) &&
        strcmp(
            command->valuestring,
            "on_channel_status"
        ) == 0) {

        cJSON* status =
            cJSON_GetObjectItem(
                root,
                "status"
            );

        if (cJSON_IsString(status)) {
            channel_online_ =
                strcmp(
                    status->valuestring,
                    "online"
                ) == 0;

            ESP_LOGI(
                TAG,
                "Channel status: %s",
                status->valuestring
            );
        }
    }

    /*
     * start_stream response.
     */
    cJSON* stream_id =
        cJSON_GetObjectItem(
            root,
            "stream_id"
        );

    if (stream_start_pending_ &&
        success &&
        cJSON_IsTrue(success) &&
        cJSON_IsNumber(stream_id)) {

        stream_id_ =
            (uint32_t)stream_id->valuedouble;

        stream_active_ = true;
        stream_start_pending_ = false;

        ESP_LOGI(
            TAG,
            "Zello stream started: %lu",
            (unsigned long)stream_id_
        );
    }

    if (cJSON_IsString(error)) {
        ESP_LOGE(
            TAG,
            "Zello error: %s",
            error->valuestring
        );

        stream_start_pending_ = false;
    }

    cJSON_Delete(root);
}

void ZelloProtocol::HandleBinary(
    const char* data,
    size_t len
) {
    /*
     * We'll add Zello -> speaker playback
     * after TX/PTT works.
     */
}