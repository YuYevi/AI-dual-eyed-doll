#include "dual_network_board.h"
#include "application.h"
#include "display.h"
#include "assets/lang_config.h"
#include "settings.h"
#include <esp_log.h>
#ifdef CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING
#include "blufi.h"
#endif

static const char *TAG = "DualNetworkBoard";

DualNetworkBoard::DualNetworkBoard(gpio_num_t ml307_tx_pin, gpio_num_t ml307_rx_pin, gpio_num_t ml307_dtr_pin, int32_t default_net_type) 
    : Board(), 
      ml307_tx_pin_(ml307_tx_pin), 
      ml307_rx_pin_(ml307_rx_pin), 
      ml307_dtr_pin_(ml307_dtr_pin) {
    
    // 从Settings加载网络类型
    network_type_ = LoadNetworkTypeFromSettings(default_net_type);
    
    // 只初始化当前网络类型对应的板卡
    InitializeCurrentBoard();
}

NetworkType DualNetworkBoard::LoadNetworkTypeFromSettings(int32_t default_net_type) {
    Settings settings("network", true);
    int network_type = settings.GetInt("type", default_net_type); // 默认使用ML307 (1)
    return network_type == 1 ? NetworkType::ML307 : NetworkType::WIFI;
}

void DualNetworkBoard::SaveNetworkTypeToSettings(NetworkType type) {
    Settings settings("network", true);
    int network_type = (type == NetworkType::ML307) ? 1 : 0;
    settings.SetInt("type", network_type);
}

void DualNetworkBoard::InitializeCurrentBoard() {
    if (network_type_ == NetworkType::ML307) {
        ESP_LOGI(TAG, "Initialize ML307 board");
        current_board_ = std::make_unique<Ml307Board>(ml307_tx_pin_, ml307_rx_pin_, ml307_dtr_pin_);
    } else {
        ESP_LOGI(TAG, "Initialize WiFi board");
        current_board_ = std::make_unique<WifiBoard>();
    }
}

void DualNetworkBoard::SwitchNetworkType() {
    auto display = GetDisplay();
    if (network_type_ == NetworkType::WIFI) {    
        SaveNetworkTypeToSettings(NetworkType::ML307);
        display->ShowNotification(Lang::Strings::SWITCH_TO_4G_NETWORK);
    } else {
        SaveNetworkTypeToSettings(NetworkType::WIFI);
        display->ShowNotification(Lang::Strings::SWITCH_TO_WIFI_NETWORK);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    auto& app = Application::GetInstance();
    app.Reboot();
}

 
std::string DualNetworkBoard::GetBoardType() {
    return current_board_->GetBoardType();
}

void DualNetworkBoard::StartNetwork() {
    auto display = Board::GetInstance().GetDisplay();
    
    if (network_type_ == NetworkType::WIFI) {
        display->SetStatus(Lang::Strings::CONNECTING);
    } else {
        display->SetStatus(Lang::Strings::DETECTING_MODULE);
    }
    current_board_->StartNetwork();
}

void DualNetworkBoard::SetNetworkEventCallback(NetworkEventCallback callback) {
    // Forward the callback to the current board
    current_board_->SetNetworkEventCallback(std::move(callback));
}

bool DualNetworkBoard::EnterBleBindMode(BleSetupMode setup_mode) {
    if (network_type_ == NetworkType::WIFI) {
        return current_board_->EnterBleBindMode(setup_mode);
    }
#ifdef CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING
    // Cellular binding must never start WiFi provisioning.
    if (setup_mode != BleSetupMode::BIND_ONLY) {
        return false;
    }
    const auto ret = Blufi::GetInstance().StartBindMode(BleSetupMode::BIND_ONLY);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start bind-only BLE: %s", esp_err_to_name(ret));
        return false;
    }
    ble_bind_mode_active_ = true;
    return true;
#else
    return Board::EnterBleBindMode(setup_mode);
#endif
}

void DualNetworkBoard::ExitBleBindMode() {
    if (network_type_ == NetworkType::WIFI) {
        current_board_->ExitBleBindMode();
        return;
    }
#ifdef CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING
    if (ble_bind_mode_active_ && Blufi::GetInstance().deinit() == ESP_OK) {
        ble_bind_mode_active_ = false;
    }
#endif
}

bool DualNetworkBoard::IsBleBindModeActive() const {
    if (network_type_ == NetworkType::WIFI) {
        return current_board_->IsBleBindModeActive();
    }
#ifdef CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING
    return ble_bind_mode_active_ && Blufi::GetInstance().IsActive();
#else
    return false;
#endif
}
NetworkInterface* DualNetworkBoard::GetNetwork() {
    return current_board_->GetNetwork();
}

const char* DualNetworkBoard::GetNetworkStateIcon() {
    return current_board_->GetNetworkStateIcon();
}

void DualNetworkBoard::SetPowerSaveLevel(PowerSaveLevel level) {
    current_board_->SetPowerSaveLevel(level);
}

std::string DualNetworkBoard::GetBoardJson() {   
    return current_board_->GetBoardJson();
}

std::string DualNetworkBoard::GetDeviceStatusJson() {
    return current_board_->GetDeviceStatusJson();
}
