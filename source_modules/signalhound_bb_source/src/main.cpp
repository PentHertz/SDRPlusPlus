// SDR++ Source Module for SignalHound BB-series devices
// Author: Sébastien Dudek (@FlUxIuS) at @Penthertz company

#include <imgui.h>
#include <utils/flog.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <gui/style.h>
#include <config.h>
#include <gui/smgui.h>
#include <thread>
#include <vector>
#include <string>
#include <cmath>
#include <atomic>

#include "bb_api.h"

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "signalhound_bb_source",
    /* Description:     */ "SignalHound BB-series source module for SDR++",
    /* Author:          */ "Sébastien Dudek (@FlUxIuS) at @Penthertz company",
    /* Version:         */ 0, 0, 1,
    /* Max instances    */ 1
};

ConfigManager config;

class SignalHoundBBModule : public ModuleManager::Instance {
public:
    SignalHoundBBModule(std::string name) {
        this->name = name;
        
        maxBandwidths = {
            27.0e6,     // Decimation 1 (40 MS/s)
            17.8e6,     // Decimation 2 (20 MS/s)  
            8.0e6,      // Decimation 4 (10 MS/s)
            3.75e6,     // Decimation 8 (5 MS/s)
            2.0e6,      // Decimation 16 (2.5 MS/s)
            1.0e6,      // Decimation 32 (1.25 MS/s)
            500.0e3,    // Decimation 64 (625 kS/s)
            250.0e3,    // Decimation 128 (312.5 kS/s)
            140.0e3,    // Decimation 256 (156.25 kS/s)
            65.0e3,     // Decimation 512 (78.125 kS/s)
            30.0e3,     // Decimation 1024 (39062.5 S/s)
            15.0e3,     // Decimation 2048 (19531.25 S/s)
            8.0e3,      // Decimation 4096 (9765.625 S/s)
            4.0e3       // Decimation 8192 (4882.8125 S/s)
        };

        decimationId = 6;
        refLevel = -20.0f;
        bandwidthHz = 500000.0f;  // 500 kHz - should be valid for most decimations
        gain = BB_AUTO_GAIN;      // Auto gain by default
        attenuation = BB_AUTO_ATTEN;  // Auto attenuation by default
        purgeIQ = false;
        actualSampleRate = 625000.0;
        configDirty = false;

        handler.ctx = this;
        handler.selectHandler = menuSelected;
        handler.deselectHandler = menuDeselected;
        handler.menuHandler = menuHandler;
        handler.startHandler = start;
        handler.stopHandler = stop;
        handler.tuneHandler = tune;
        handler.stream = &stream;

        refresh();

        int devSerial = 0;
        config.acquire();
        if (config.conf.contains("device")) { devSerial = config.conf["device"]; }
        config.release();
        selectBySerial(devSerial);

        sigpath::sourceManager.registerSource("SignalHound BB", &handler);
    }

    ~SignalHoundBBModule() { 
        stop(this); 
        sigpath::sourceManager.unregisterSource("SignalHound BB"); 
    }
    
    void postInit() {}
    void enable() { enabled = true; }
    void disable() { enabled = false; }
    bool isEnabled() { return enabled; }

    void refresh() {
        devList.clear(); 
        devListTxt = "";
        int serials[BB_MAX_DEVICES], count;
        if (bbGetSerialNumberList(serials, &count) == bbNoError && count > 0) {
            for (int i = 0; i < count; i++) {
                devList.push_back(serials[i]);
                devListTxt += std::to_string(serials[i]) + '\0';
            }
        }
    }
    
    void selectFirst() { 
        if (!devList.empty()) { 
            selectBySerial(devList[0]); 
        } 
    }

    void selectBySerial(int serial) {
        devId = -1;
        for (int i = 0; i < devList.size(); i++) {
            if (devList[i] == serial) { 
                devId = i; 
                break; 
            }
        }
        if (devId == -1) {
            if (devList.empty()) return;
            devId = 0;
        }
        selectedSerial = devList[devId];
        selectedSerStr = std::to_string(selectedSerial);

        config.acquire();
        if (!config.conf["devices"].contains(selectedSerStr)) {
            config.conf["devices"][selectedSerStr]["decimationId"] = 6;
            config.conf["devices"][selectedSerStr]["refLevel"] = -20.0;
            config.conf["devices"][selectedSerStr]["bandwidth"] = 500000.0; // 500 kHz default
            config.conf["devices"][selectedSerStr]["gain"] = BB_AUTO_GAIN;
            config.conf["devices"][selectedSerStr]["attenuation"] = BB_AUTO_ATTEN;
            config.release(true);
        } else {
            json devConf = config.conf["devices"][selectedSerStr];
            if (devConf.contains("decimationId")) { 
                decimationId = devConf["decimationId"]; 
            }
            if (devConf.contains("refLevel")) { 
                refLevel = devConf["refLevel"]; 
            }
            if (devConf.contains("bandwidth")) { 
                bandwidthHz = devConf["bandwidth"];
                // Ensure loaded bandwidth is reasonable
                if (bandwidthHz < 1000.0) {
                    flog::warn("SignalHound BB: Loaded bandwidth {0} too small, using 500kHz", bandwidthHz);
                    bandwidthHz = 500000.0;
                }
            }
            if (devConf.contains("gain")) {
                gain = devConf["gain"];
            }
            if (devConf.contains("attenuation")) {
                attenuation = devConf["attenuation"];
            }
            config.release();
        }
    }

private:
    static std::string getRateScaled(double bw) {
        char buf[1024];
        if (bw >= 1000000.0) { 
            sprintf(buf, "%.3f Msps", bw / 1000000.0); 
        } 
        else if (bw >= 1000.0) { 
            sprintf(buf, "%.2f Ksps", bw / 1000.0); 
        } 
        else { 
            sprintf(buf, "%.2f Sps", bw); 
        }
        return std::string(buf);
    }
    
    bool check_status(bbStatus status, const std::string& context) {
        if (status < 0) {
            // Negative codes are actual errors
            flog::error("SignalHound BB Error in {0}: {1} (code {2})", 
                       context, bbGetErrorString(status), static_cast<int>(status));
            return false;
        } else if (status > 0) {
            // Positive codes are warnings - log but continue
            flog::warn("SignalHound BB Warning in {0}: {1} (code {2})", 
                      context, bbGetErrorString(status), static_cast<int>(status));
        }
        return true;
    }

    static void menuSelected(void* ctx) {
        SignalHoundBBModule* _this = (SignalHoundBBModule*)ctx;
        core::setInputSampleRate(_this->actualSampleRate);
    }

    static void menuDeselected(void* ctx) {}

    static void start(void* ctx) {
        SignalHoundBBModule* _this = (SignalHoundBBModule*)ctx;
        if (_this->running.load() || _this->selectedSerial == 0) return;

        // Open device by serial number
        flog::info("SignalHound BB: Opening device with serial {0}", _this->selectedSerial);
        if (!_this->check_status(bbOpenDeviceBySerialNumber(&_this->deviceHandle, _this->selectedSerial), 
                                "bbOpenDeviceBySerialNumber")) {
            return;
        }
        
        // Calculate decimation factor (power of 2)
        int decimation = static_cast<int>(std::pow(2, _this->decimationId));
        
        // Validate and adjust bandwidth for the given decimation
        // According to API docs, there are minimum bandwidth requirements per decimation
        double minBandwidth = 200.0; // Absolute minimum
        double maxBandwidth = _this->maxBandwidths[_this->decimationId];
        
        // Ensure bandwidth is within valid range
        if (_this->bandwidthHz < minBandwidth) {
            flog::warn("SignalHound BB: Bandwidth {0} too low, setting to minimum {1}", 
                      _this->bandwidthHz, minBandwidth);
            _this->bandwidthHz = minBandwidth;
        }
        if (_this->bandwidthHz > maxBandwidth) {
            flog::warn("SignalHound BB: Bandwidth {0} too high, setting to maximum {1}", 
                      _this->bandwidthHz, maxBandwidth);
            _this->bandwidthHz = maxBandwidth;
        }
        
        flog::info("SignalHound BB: Configuring with decimation={0}, bandwidth={1}, refLevel={2}", 
                  decimation, _this->bandwidthHz, _this->refLevel);
        
        // Configure device using current API functions (not deprecated ones)
        if (!_this->check_status(bbConfigureRefLevel(_this->deviceHandle, _this->refLevel), "bbConfigureRefLevel") ||
            !_this->check_status(bbConfigureGainAtten(_this->deviceHandle, _this->gain, _this->attenuation), "bbConfigureGainAtten") ||
            !_this->check_status(bbConfigureIQCenter(_this->deviceHandle, _this->freq), "bbConfigureIQCenter") ||
            !_this->check_status(bbConfigureIQ(_this->deviceHandle, decimation, _this->bandwidthHz), "bbConfigureIQ")) {
            bbCloseDevice(_this->deviceHandle);
            _this->deviceHandle = -1;
            return;
        }
        
        // Initiate I/Q streaming mode first
        if (!_this->check_status(bbInitiate(_this->deviceHandle, BB_STREAMING, BB_STREAM_IQ), "bbInitiate")) {
            bbCloseDevice(_this->deviceHandle);
            _this->deviceHandle = -1;
            return;
        }
        
        // Now query actual stream parameters (after initiate)
        double actual_sample_rate, actual_bw;
        if (!_this->check_status(bbQueryIQParameters(_this->deviceHandle, &actual_sample_rate, &actual_bw), 
                                "bbQueryIQParameters")) {
            bbAbort(_this->deviceHandle);
            bbCloseDevice(_this->deviceHandle);
            _this->deviceHandle = -1;
            return;
        }
        
        _this->actualSampleRate = actual_sample_rate;
        flog::info("SignalHound BB: Device started with rate {0} sps, bandwidth {1} Hz.", 
                  _this->actualSampleRate, actual_bw);

        // Set the input sample rate for SDR++
        core::setInputSampleRate(_this->actualSampleRate);
        
        _this->running.store(true);
        _this->rxThread = std::thread(rx_thread_func, _this);
        
        flog::info("SignalHound BB: Successfully started streaming");
    }

    static void stop(void* ctx) {
        SignalHoundBBModule* _this = (SignalHoundBBModule*)ctx;
        if (!_this->running.load()) return;
        
        flog::info("SignalHound BB: Stopping...");
        
        _this->running.store(false);
        _this->stream.stopWriter();
        
        if (_this->rxThread.joinable()) { 
            _this->rxThread.join(); 
        }
        
        if (_this->deviceHandle != -1) {
            bbAbort(_this->deviceHandle);
            bbCloseDevice(_this->deviceHandle);
            _this->deviceHandle = -1;
        }
        
        _this->stream.clearWriteStop();
        flog::info("SignalHound BB: Stopped");
    }

    static void tune(double freq, void* ctx) {
        SignalHoundBBModule* _this = (SignalHoundBBModule*)ctx;
        _this->freq = freq;
        if (_this->running.load() && _this->deviceHandle != -1) {
            restartStreamWithNewParams(_this, "frequency change");
        }
    }
    
    // Helper function to restart streaming with new parameters
    static void restartStreamWithNewParams(SignalHoundBBModule* _this, const std::string& reason) {
        if (!_this->running.load() || _this->deviceHandle == -1) return;
        
        flog::info("SignalHound BB: Restarting stream for {0}", reason);
        
        // Stop the RX thread temporarily
        _this->running.store(false);
        _this->stream.stopWriter();
        
        if (_this->rxThread.joinable()) {
            _this->rxThread.join();
        }
        
        // Abort current streaming
        bbAbort(_this->deviceHandle);
        
        // Calculate decimation factor
        int decimation = static_cast<int>(std::pow(2, _this->decimationId));
        
        // Validate bandwidth
        double minBandwidth = 200.0;
        double maxBandwidth = _this->maxBandwidths[_this->decimationId];
        if (_this->bandwidthHz < minBandwidth) {
            _this->bandwidthHz = minBandwidth;
        }
        if (_this->bandwidthHz > maxBandwidth) {
            _this->bandwidthHz = maxBandwidth;
        }
        
        // Reconfigure device parameters
        if (!_this->check_status(bbConfigureRefLevel(_this->deviceHandle, _this->refLevel), "bbConfigureRefLevel (restart)") ||
            !_this->check_status(bbConfigureGainAtten(_this->deviceHandle, _this->gain, _this->attenuation), "bbConfigureGainAtten (restart)") ||
            !_this->check_status(bbConfigureIQCenter(_this->deviceHandle, _this->freq), "bbConfigureIQCenter (restart)") ||
            !_this->check_status(bbConfigureIQ(_this->deviceHandle, decimation, _this->bandwidthHz), "bbConfigureIQ (restart)")) {
            flog::error("SignalHound BB: Failed to reconfigure device during restart");
            return;
        }
        
        // Restart streaming
        if (!_this->check_status(bbInitiate(_this->deviceHandle, BB_STREAMING, BB_STREAM_IQ), "bbInitiate (restart)")) {
            flog::error("SignalHound BB: Failed to restart streaming");
            return;
        }
        
        // Query updated parameters
        double actual_sample_rate, actual_bw;
        if (_this->check_status(bbQueryIQParameters(_this->deviceHandle, &actual_sample_rate, &actual_bw), "bbQueryIQParameters (restart)")) {
            _this->actualSampleRate = actual_sample_rate;
            core::setInputSampleRate(_this->actualSampleRate);
            flog::info("SignalHound BB: Stream restarted - Rate: {0} sps, BW: {1} Hz, Ref: {2} dBm", 
                      _this->actualSampleRate, actual_bw, _this->refLevel);
        }
        
        // Clear stream and restart RX thread
        _this->stream.clearWriteStop();
        _this->running.store(true);
        _this->rxThread = std::thread(rx_thread_func, _this);
    }
    
    // Helper function to update reference level dynamically
    static void updateRefLevel(SignalHoundBBModule* _this) {
        if (_this->running.load() && _this->deviceHandle != -1) {
            restartStreamWithNewParams(_this, "reference level change");
        }
    }
    
    // Helper function to update bandwidth dynamically  
    static void updateBandwidth(SignalHoundBBModule* _this) {
        if (_this->running.load() && _this->deviceHandle != -1) {
            restartStreamWithNewParams(_this, "bandwidth/decimation change");
        }
    }
    
    static void menuHandler(void* ctx) {
        SignalHoundBBModule* _this = (SignalHoundBBModule*)ctx;
        
        bool wasRunning = _this->running.load();
        
        if (wasRunning) SmGui::BeginDisabled();
        
        SmGui::FillWidth();
        if (SmGui::Combo(CONCAT("##_sh_dev_sel_", _this->name), &_this->devId, _this->devListTxt.c_str())) {
            _this->selectBySerial(_this->devList[_this->devId]);
            _this->configDirty = true;
        }
        SmGui::SameLine();
        if (SmGui::Button(CONCAT("Refresh##_sh_refr_", _this->name))) { 
            _this->refresh(); 
        }
        
        const double nativeRate = 40.0e6;
        std::string decimationTxt;
        for (int i = 0; i <= 13; i++) {
            int decimation = static_cast<int>(std::pow(2, i));
            double rate = nativeRate / decimation;
            decimationTxt += "x" + std::to_string(decimation) + " (" + getRateScaled(rate) + ")";
            decimationTxt += '\0';
        }
        
        SmGui::LeftLabel("Decimation"); SmGui::FillWidth();
        if (SmGui::Combo(CONCAT("##_sh_decim_sel_", _this->name), &_this->decimationId, decimationTxt.c_str())) {
            double newMaxBw = _this->maxBandwidths[_this->decimationId];
            if (_this->bandwidthHz > newMaxBw) { 
                _this->bandwidthHz = newMaxBw; 
            }
            _this->configDirty = true;
            // Update bandwidth dynamically if running (causes stream restart)
            if (wasRunning) {
                updateBandwidth(_this);
            }
        }
        if (wasRunning && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Changing decimation will briefly restart the stream (~0.5s gap)");
        }

        double maxBwForCurrentDecimation = _this->maxBandwidths[_this->decimationId];
        SmGui::LeftLabel("IF Bandwidth (Hz)"); SmGui::FillWidth();
        if (SmGui::SliderFloat(CONCAT("##_sh_bw_", _this->name), &_this->bandwidthHz, 
                              200.0f, maxBwForCurrentDecimation, SmGui::FMT_STR_FLOAT_NO_DECIMAL, 
                              ImGuiSliderFlags_Logarithmic)) {
            _this->configDirty = true;
            // Update bandwidth dynamically if running (causes stream restart)
            if (wasRunning) {
                updateBandwidth(_this);
            }
        }
        if (wasRunning && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Changing bandwidth will briefly restart the stream (~0.5s gap)");
        }

        if (wasRunning) SmGui::EndDisabled();

        // Gain control (can be adjusted while running)
        SmGui::LeftLabel("Gain"); SmGui::FillWidth();
        // Build null-terminated string properly
        std::string gainTxt = "Auto";
        gainTxt += '\0';
        gainTxt += "0 dB";
        gainTxt += '\0';
        gainTxt += "5 dB";
        gainTxt += '\0';
        gainTxt += "15/30 dB";
        gainTxt += '\0';
        gainTxt += "20/35 dB";
        gainTxt += '\0';
        
        int gainComboId = _this->gain + 1;  // +1 because Auto is -1, so Auto becomes 0
        if (SmGui::Combo(CONCAT("##_sh_gain_sel_", _this->name), &gainComboId, gainTxt.c_str())) {
            _this->gain = gainComboId - 1;  // Convert back: 0 becomes -1 (Auto)
            _this->configDirty = true;
            if (wasRunning) {
                restartStreamWithNewParams(_this, "gain change");
            }
        }
        if (wasRunning && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Changing gain will briefly restart the stream (~0.5s gap)");
        }

        // Attenuation control (can be adjusted while running)
        SmGui::LeftLabel("Attenuation"); SmGui::FillWidth();
        // Build null-terminated string properly
        std::string attenTxt = "Auto";
        attenTxt += '\0';
        attenTxt += "0 dB";
        attenTxt += '\0';
        attenTxt += "10 dB";
        attenTxt += '\0';
        attenTxt += "20 dB";
        attenTxt += '\0';
        attenTxt += "30 dB";
        attenTxt += '\0';
        
        int attenComboId = _this->attenuation + 1;  // +1 because Auto is -1
        if (SmGui::Combo(CONCAT("##_sh_atten_sel_", _this->name), &attenComboId, attenTxt.c_str())) {
            _this->attenuation = attenComboId - 1;  // Convert back: 0 becomes -1 (Auto)
            _this->configDirty = true;
            if (wasRunning) {
                restartStreamWithNewParams(_this, "attenuation change");
            }
        }
        if (wasRunning && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Changing attenuation will briefly restart the stream (~0.5s gap)");
        }

        // Reference level can be adjusted while running (will cause restart)
        SmGui::LeftLabel("Ref. Level (dBm)"); SmGui::FillWidth();
        if (SmGui::SliderFloat(CONCAT("##_sh_ref_", _this->name), &_this->refLevel, 
                              -100.0f, BB_MAX_REFERENCE)) {
            _this->configDirty = true;
            // Update reference level dynamically if running (causes stream restart)
            if (wasRunning) {
                updateRefLevel(_this);
            }
        }
        if (wasRunning && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Changing reference level will briefly restart the stream (~0.5s gap)");
        }

        SmGui::ForceSync();
        SmGui::Checkbox(CONCAT("Purge Buffer##_sh_purge_", _this->name), &_this->purgeIQ);

        // Save configuration changes
        if (_this->configDirty) {
            config.acquire();
            config.conf["device"] = _this->selectedSerial;
            config.conf["devices"][_this->selectedSerStr]["decimationId"] = _this->decimationId;
            config.conf["devices"][_this->selectedSerStr]["bandwidth"] = _this->bandwidthHz;
            config.conf["devices"][_this->selectedSerStr]["refLevel"] = _this->refLevel;
            config.conf["devices"][_this->selectedSerStr]["gain"] = _this->gain;
            config.conf["devices"][_this->selectedSerStr]["attenuation"] = _this->attenuation;
            config.release(true);
            _this->configDirty = false;
        }
    }
    
    static void rx_thread_func(void* ctx) {
        SignalHoundBBModule* _this = (SignalHoundBBModule*)ctx;
        
        // Determine buffer size - use a reasonable default
        const int bufferSize = 16384; // 16K complex samples
        std::vector<dsp::complex_t> iq_buffer(bufferSize);
        
        bbIQPacket pkt;
        pkt.iqData = iq_buffer.data();
        pkt.iqCount = bufferSize;
        pkt.triggers = nullptr;  // Not using triggers for basic operation
        pkt.triggerCount = 0;
        pkt.purge = 0;  // Don't purge on first call
        
        flog::info("SignalHound BB: RX thread started");
        
        while (_this->running.load()) {
            // Set purge flag if requested
            pkt.purge = _this->purgeIQ ? 1 : 0;
            if (_this->purgeIQ) { 
                _this->purgeIQ = false; 
            }
            
            // Get I/Q data from device
            bbStatus status = bbGetIQ(_this->deviceHandle, &pkt);
            
            if (status != bbNoError) {
                if (status == bbDeviceNotStreamingErr) {
                    flog::warn("SignalHound BB: Device not streaming, retrying...");
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                } else {
                    flog::error("SignalHound BB: bbGetIQ failed: {0}", bbGetErrorString(status));
                    break;
                }
            }
            
            // Check if we got valid data
            if (pkt.iqCount > 0) {
                // Copy data to SDR++ stream
                if (pkt.iqCount <= bufferSize) {
                    memcpy(_this->stream.writeBuf, pkt.iqData, pkt.iqCount * sizeof(dsp::complex_t));
                    if (!_this->stream.swap(pkt.iqCount)) {
                        break;  // Stream was stopped
                    }
                } else {
                    flog::warn("SignalHound BB: Received more data than buffer size: {0} > {1}", 
                              pkt.iqCount, bufferSize);
                }
                
                // Report sample loss if detected
                if (pkt.sampleLoss) {
                    flog::warn("SignalHound BB: Sample loss detected");
                }
            } else {
                // No data received, short sleep to prevent busy waiting
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        
        flog::info("SignalHound BB: RX thread stopped");
    }

    std::string name;
    dsp::stream<dsp::complex_t> stream;
    SourceManager::SourceHandler handler;
    bool enabled = true;
    std::atomic<bool> running{false};
    bool configDirty = false;
    std::thread rxThread;
    int deviceHandle = -1;
    double freq = 100e6;
    int selectedSerial = 0;
    std::string selectedSerStr = "";
    int devId = 0;
    std::vector<int> devList;
    std::string devListTxt;
    int decimationId;
    float bandwidthHz;
    float refLevel;
    int gain;
    int attenuation;
    bool purgeIQ;
    double actualSampleRate;
    std::vector<double> maxBandwidths;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    def["devices"] = json({});
    def["device"] = 0;
    config.setPath(core::args["root"].s() + "/signalhound_bb_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) { 
    return new SignalHoundBBModule(name); 
}

MOD_EXPORT void _DELETE_INSTANCE_(ModuleManager::Instance* instance) { 
    delete (SignalHoundBBModule*)instance; 
}

MOD_EXPORT void _END_() { 
    config.disableAutoSave(); 
    config.save(); 
}
