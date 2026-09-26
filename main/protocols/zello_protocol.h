#ifndef _ZELLO_PROTOCOL_H_
#define _ZELLO_PROTOCOL_H_

#include "protocol.h"

#include <web_socket.h>
#include <memory>
#include <string>
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

class ZelloProtocol : public Protocol {
public:
    ZelloProtocol(
        const std::string& username,
        const std::string& password,
        const std::string& auth_token,
        const std::string& channel
    );

    ~ZelloProtocol();

    bool Start() override;

    bool SendAudio(
        std::unique_ptr<AudioStreamPacket> packet
    ) override;

    bool OpenAudioChannel() override;

    void CloseAudioChannel(
        bool send_goodbye = true
    ) override;

    bool IsAudioChannelOpened() const override;
    void SendStartListening(ListeningMode mode) override;
    void SendStopListening() override;

    bool StartStream();
    void StopStream();

private:
    std::unique_ptr<WebSocket> websocket_;

    std::string username_;
    std::string password_;
    std::string auth_token_;
    std::string channel_;

    int sequence_ = 1;

    uint32_t stream_id_ = 0;
    uint32_t incoming_stream_id_ = 0;
    int incoming_sample_rate_ = 16000;
    int incoming_frame_duration_ = 60;

    bool logged_in_ = false;
    bool channel_online_ = false;
    bool stream_active_ = false;
    bool stream_start_pending_ = false;

    bool SendText(const std::string& text) override;

    void HandleJson(
        const char* data,
        size_t len
    );

    void HandleBinary(
        const char* data,
        size_t len
    );
};

#endif