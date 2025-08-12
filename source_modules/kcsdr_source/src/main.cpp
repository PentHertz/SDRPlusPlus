// SDR++ Source Module for KC SDR devices
// Author: Sébastien Dudek (@FlUxIuS) at @Penthertz company
// Version: 1.0.1 - Working version

#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <gui/smgui.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <utils/optionlist.h>
#include <utils/flog.h>
#include <config.h>
#include <dsp/stream.h>
#include <volk/volk.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <string>
#include <vector>

#include "kcsdr.h"

SDRPP_MOD_INFO{
    /* Name:            */ "kcsdr_source",
    /* Description:     */ "KC SDR Source Module",
    /* Author:          */ "Ryzerth beginning, and completed and fixed by Sébastien Dudek",
    /* Version:         */ 1, 0, 1,
    /* Max instances    */ -1
};

ConfigManager config;

class KCSDRSourceModule : public ModuleManager::Instance {
public:
    KCSDRSourceModule(std::string name) {
        this->name = name;
        samplerates.define(0, "5 MHz", 5e6);
        samplerates.define(1, "10 MHz", 10e6);
        samplerates.define(2, "15 MHz", 15e6);
        samplerates.define(3, "20 MHz", 20e6);
        samplerates.define(4, "25 MHz", 25e6);
        samplerates.define(5, "30 MHz", 30e6);
        samplerates.define(6, "35 MHz", 35e6);
        samplerates.define(7, "40 MHz", 40e6);

        deviceTypes.define(0, "KC 908-1", KC_908_1);
        deviceTypes.define(1, "KC 908-N", KC_908_N);
        
        ports.define(0, "Port 1", 1); ports.define(1, "Port 2", 2);

        handler.ctx = this;
        handler.selectHandler = menuSelected; handler.deselectHandler = menuDeselected;
        handler.menuHandler = menuHandler;     handler.startHandler = start;
        handler.stopHandler = stop;            handler.tuneHandler = tune;
        handler.stream = &stream;

        api = kcsdr_init();
        if (!api) flog::error("KC SDR: Failed to initialize API");
        
        loadConfig();
        sigpath::sourceManager.registerSource("KC SDR", &handler);
    }

    ~KCSDRSourceModule() {
        stop(this);
        if (currentDevice && api) {
            api->close(currentDevice);
            currentDevice = nullptr;
        }
        sigpath::sourceManager.unregisterSource("KC SDR");
    }

    void postInit() {}
    void enable() { enabled = true; }
    void disable() { enabled = false; }
    bool isEnabled() { return enabled; }

private:
    void loadConfig() {
        config.acquire();
        if (config.conf.contains(name)) {
            json cfg = config.conf[name];
            devId = cfg.value("devId", 0);
            sampleRate = cfg.value("sampleRate", 10e6);
            selectedPort = cfg.value("port", 1);
            att = cfg.value("att", 0); gain = cfg.value("gain", 15); extGain = cfg.value("extGain", 1);
            for (int i = 0; i < samplerates.size(); i++) {
                if (samplerates.value(i) == sampleRate) { srId = i; break; }
            }
            for (int i = 0; i < ports.size(); i++) {
                if (ports.value(i) == selectedPort) { portId = i; break; }
            }
        }
        config.release();
    }

    void saveConfig() {
        config.acquire();
        config.conf[name]["devId"] = devId; config.conf[name]["sampleRate"] = sampleRate;
        config.conf[name]["port"] = selectedPort; config.conf[name]["att"] = att;
        config.conf[name]["gain"] = gain; config.conf[name]["extGain"] = extGain;
        config.release(true);
    }

    static void menuSelected(void* ctx) { ((KCSDRSourceModule*)ctx)->coreSetInputSampleRate(); }
    static void menuDeselected(void* ctx) {}

    static void start(void* ctx) {
        KCSDRSourceModule* _this = (KCSDRSourceModule*)ctx;
        if (_this->running) return;
        if (!_this->api) { flog::error("KC SDR: API not initialized."); return; }

        if (!_this->currentDevice) {
            device_type selectedType = _this->deviceTypes.value(_this->devId);
            _this->currentDevice = _this->api->find(selectedType);
            if (!_this->currentDevice) { flog::error("KC SDR: Failed to find device."); return; }
        }
        flog::info("KC SDR: Device '{0}' found. Configuring...", _this->currentDevice->name);

        _this->api->rx_port(_this->currentDevice, _this->selectedPort);
        _this->api->rx_freq(_this->currentDevice, _this->freq);
        _this->api->rx_att(_this->currentDevice, _this->att);
        _this->api->rx_amp(_this->currentDevice, _this->gain);
        _this->api->rx_ext_amp(_this->currentDevice, _this->extGain);
        _this->api->rx_bw(_this->currentDevice, (uint64_t)_this->sampleRate);
        
        _this->coreSetInputSampleRate();
        
        flog::info("KC SDR: Starting stream...");
        _this->api->rx_start(_this->currentDevice);
        
        _this->run = true;
        _this->workerThread = std::thread(&KCSDRSourceModule::worker, _this);
        _this->running = true;
        flog::info("KC SDR: Started successfully.");
    }

    static void stop(void* ctx) {
        KCSDRSourceModule* _this = (KCSDRSourceModule*)ctx;
        if (!_this->running && !_this->run) return;
        
        _this->run = false;

        // Stop the hardware stream first. This should immediately unblock
        // the worker thread if it's stuck in a blocking api->read() call.
        if (_this->currentDevice && _this->api) {
            flog::info("KC SDR: Stopping hardware stream...");
            _this->api->rx_stop(_this->currentDevice);
        }

        // Signal the SDR++ stream that we are done writing. This will cause
        // any pending swap() call to fail gracefully and prevent ring buffer messages.
        _this->stream.stopWriter();

        // Now, wait for the worker thread to fully exit.
        if (_this->workerThread.joinable()) {
            _this->workerThread.join();
        }

        // Clean up the stream state for a potential restart.
        _this->stream.clearWriteStop();

        _this->running = false;
        flog::info("KC SDR: Device stopped cleanly.");
    }

    static void tune(double freq, void* ctx) {
        KCSDRSourceModule* _this = (KCSDRSourceModule*)ctx;
        _this->freq = freq;
        if (_this->running) _this->api->rx_freq(_this->currentDevice, freq);
    }

    static void menuHandler(void* ctx) {
        KCSDRSourceModule* _this = (KCSDRSourceModule*)ctx;
        
        if (_this->running) SmGui::BeginDisabled();
        
        if (SmGui::Combo("Device Type", &_this->devId, _this->deviceTypes.txt)) _this->saveConfig();
        
        if (SmGui::Combo("Sample Rate", &_this->srId, _this->samplerates.txt)) {
            _this->sampleRate = _this->samplerates.value(_this->srId);
            _this->saveConfig();
        }
        
        if (SmGui::Combo("RX Port", &_this->portId, _this->ports.txt)) {
            _this->selectedPort = _this->ports.value(_this->portId);
            _this->saveConfig();
        }
        
        if (_this->running) SmGui::EndDisabled();
        
        if (!_this->running) {
             SmGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "NOTE: Stop and Start to apply changes.");
        }
        
        int att_i = _this->att, gain_i = _this->gain, extGain_i = _this->extGain;

        SmGui::LeftLabel("Attenuation"); SmGui::FillWidth();
        if (SmGui::SliderInt("##_att", &att_i, 0, 31)) {
            _this->att = att_i;
            if (_this->running) _this->api->rx_att(_this->currentDevice, _this->att);
            _this->saveConfig();
        }
        
        SmGui::LeftLabel("Gain (IF)"); SmGui::FillWidth();
        if (SmGui::SliderInt("##_gain", &gain_i, 0, 30)) {
            _this->gain = gain_i;
            if (_this->running) _this->api->rx_amp(_this->currentDevice, _this->gain);
            _this->saveConfig();
        }

        SmGui::LeftLabel("Gain (Ext)"); SmGui::FillWidth();
        if (SmGui::SliderInt("##_extgain", &extGain_i, 0, 40)) {
            _this->extGain = extGain_i;
            if (_this->running) _this->api->rx_ext_amp(_this->currentDevice, _this->extGain);
            _this->saveConfig();
        }
    }
    
    void worker() {
        flog::info("KC SDR: Worker thread started");
        const int SAMPLES_PER_READ = 16384;
        const int BYTES_PER_READ = SAMPLES_PER_READ * 2 * sizeof(int16_t);
        std::vector<int16_t> readBuffer(SAMPLES_PER_READ * 2);

        while (run) {
            if (!currentDevice) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                continue;
            }

            bool success = api->read(currentDevice, (uint8_t*)readBuffer.data(), BYTES_PER_READ);
            
            // If read returns false, it could be because we are stopping.
            // The `run` flag is the final authority.
            if (!success) {
                if (!run) break; // Exit loop if we were told to stop
                continue; // Otherwise, it was just a timeout, try again
            }

            dsp::complex_t* out = stream.writeBuf;
            float scale = 1.0f / 32767.0f;
            
            for (int i = 0; i < SAMPLES_PER_READ; i++) {
                out[i].re = readBuffer[i * 2] * scale;
                out[i].im = readBuffer[i * 2 + 1] * scale;
            }
            
            if (!stream.swap(SAMPLES_PER_READ)) {
                // swap() returned false, meaning the stream was stopped. Exit cleanly.
                break;
            }
        }
        flog::info("KC SDR: Worker thread stopped");
    }

    void coreSetInputSampleRate() { 
        core::setInputSampleRate(sampleRate);
        flog::info("KC SDR: Reporting {0:.2f} MHz to SDR++ core", (sampleRate / 1e6));
    }

private:
    std::string name;
    bool enabled = true;
    dsp::stream<dsp::complex_t> stream;
    
    sdr_api* api = nullptr;
    sdr_obj* currentDevice = nullptr;
    
    OptionList<int, device_type> deviceTypes;
    OptionList<int, double> samplerates;
    OptionList<int, int> ports;
    
    std::atomic<bool> running = false;
    double sampleRate = 10e6;
    SourceManager::SourceHandler handler;
    double freq = 100e6;
    int devId = 0;
    int srId = 1;
    int portId = 0;
    uint8_t selectedPort = 1; 
    uint8_t att = 0;
    uint8_t gain = 15;
    uint8_t extGain = 1;
    std::thread workerThread;
    std::atomic<bool> run = false;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    config.setPath(core::args["root"].s() + "/kcsdr_config.json");
    config.load(def); config.enableAutoSave();
}
MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new KCSDRSourceModule(name);
}
MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (KCSDRSourceModule*)instance;
}
MOD_EXPORT void _END_() {
    config.disableAutoSave(); config.save();
}
