/*
 * Copyright 2026, Kris Beazley HaikuDVR@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <Application.h>
#include <OS.h>
#include <SupportDefs.h>
#include <Locker.h>
#include <curl/curl.h>
#include <fstream>
#include <string>
#include <vector>
#include <ctime>
#include <signal.h>
#include <unistd.h> 
#include <nlohmann/json.hpp>
#include "hdhomerun.h"

using json = nlohmann::json;

const char* kSettingsFilePath = "/boot/home/config/settings/HaikuDVR_schedules.json";

int32 gStopService = 0;
int32 gCancelRecording = 0; 

struct ScheduleItem {
    std::string startDate;
    std::string startTime;
    std::string channel;
    std::string duration;
    std::string tunerIp; 
    bool processed;
};


struct RecordingConfig {
    std::string ip;
    std::string channel;
    std::string duration;
    std::string path;
    int dbIndexPosition;
};

std::vector<ScheduleItem> gScheduleList;
std::string gGlobalSaveDirectory = "/boot/home";
BLocker gScheduleLocker;

void SignalExitInterceptor(int signalType) { 
    atomic_set(&gStopService, 1);
    atomic_set(&gCancelRecording, 1);
}


std::vector<std::string> DiscoverAllTuners() {
    std::vector<std::string> tuners;
    struct hdhomerun_discover_device_t result_list[64];
    hdhomerun_discover_t* ds = hdhomerun_discover_create(NULL);
    if (ds == NULL) {
        tuners.push_back("192.168.0.100");
        return tuners;
    }
    int count = hdhomerun_discover_find_devices_v2(ds, 0, HDHOMERUN_DEVICE_TYPE_TUNER, HDHOMERUN_DEVICE_ID_WILDCARD, result_list, 64);
    for (int i = 0; i < count; i++) {
        uint32_t ip = result_list[i].ip_addr;
        char ip_str[32];
        sprintf(ip_str, "%u.%u.%u.%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
        tuners.push_back(std::string(ip_str));
    }
    hdhomerun_discover_destroy(ds);
    if (tuners.empty()) tuners.push_back("192.168.0.100");
    return tuners;
}

// Storage configuration loader
void LoadSchedulesFromDisk() {
    std::ifstream file(kSettingsFilePath);
    if (!file.is_open()) return;

    try {
        json jIn;
        file >> jIn;
        gScheduleLocker.Lock();
        
        if (jIn.is_object() && jIn.contains("save_directory")) {
            gGlobalSaveDirectory = jIn.value("save_directory", "/boot/home");

            if (jIn.contains("schedules") && jIn["schedules"].is_array()) {
                gScheduleList.clear();
                for (const auto& entry : jIn["schedules"]) {
                    ScheduleItem item;
                    item.startDate = entry.value("date", "2026-06-13"); 
                    item.startTime = entry.value("time", "12:00");
                    item.channel = entry.value("channel", "5.1");
                    item.duration = entry.value("duration", "1800");                    
                    item.tunerIp = entry.value("tuner_ip", ""); 
                    
                    item.processed = entry.value("processed", false);
                    gScheduleList.push_back(item);
                }
            }
        }
        gScheduleLocker.Unlock();
    } catch (...) {
        gScheduleLocker.Unlock();
    }
    file.close();
}


void SaveSchedulesToDisk() {
    gScheduleLocker.Lock();
    
    // Structure the root container as an object instead of a flat array
    json jRoot = json::object();
    jRoot["save_directory"] = gGlobalSaveDirectory;
    
    json jSchedules = json::array();
    for (const auto& item : gScheduleList) {
        jSchedules.push_back({
            {"date", item.startDate},
            {"time", item.startTime},
            {"channel", item.channel},
            {"duration", item.duration},
            {"processed", item.processed},
            {"tuner_ip", item.tunerIp}    
        });
    }
    jRoot["schedules"] = jSchedules;
    gScheduleLocker.Unlock();
    
    std::ofstream file(kSettingsFilePath);
    if (file.is_open()) {
        file << jRoot.dump(4);
        file.close();
    }
}


size_t StorageWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    std::ofstream* file = static_cast<std::ofstream*>(userp);
    size_t totalSize = size * nmemb;
    file->write(static_cast<char*>(contents), totalSize);
    return totalSize;
}

int32 BackgroundRecordingWorker(void* data) {
    RecordingConfig* config = static_cast<RecordingConfig*>(data);
    CURL* curl = curl_easy_init();
    if (curl) {
        std::string cleanDuration = config->duration;
        std::string url = "http://" + config->ip + ":5004/auto/v" + config->channel + "?duration=" + cleanDuration;    
        std::ofstream outputFile(config->path, std::ios::binary);
        if (outputFile.is_open()) {
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StorageWriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outputFile);
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);

            curl_easy_perform(curl);
            outputFile.close();
        } else {
            printf("[WORKER ERROR] Failed to open target file for writing! Check directory permissions.\n");
        }
        curl_easy_cleanup(curl);
        
        gScheduleLocker.Lock();
        if (config->dbIndexPosition >= 0 && (size_t)config->dbIndexPosition < gScheduleList.size()) {
            gScheduleList[config->dbIndexPosition].processed = 2; 
        }
        gScheduleLocker.Unlock();
        SaveSchedulesToDisk();
    }
    delete config;
    return 0;
}

int32 ServiceSchedulerLoop(void* data) {    
    while (atomic_get(&gStopService) == 0) {
        snooze(5000000);
        LoadSchedulesFromDisk();
        std::time_t now = std::time(nullptr);
        std::time_t lookAheadTime = now + 60; 
        std::tm* localTime = std::localtime(&lookAheadTime);
        
        char dateBuf[32];
        char timeBuf[32];
        std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", localTime);
        std::strftime(timeBuf, sizeof(timeBuf), "%H:%M", localTime);
        
        std::string curDate(dateBuf);
        std::string curTime(timeBuf);
        bool databaseUpdated = false;

        gScheduleLocker.Lock();
         /*
        size_t totalItems = gScheduleList.size();
       
        if (totalItems > 0) {
            printf("[SERVER DEBUG] System Evaluation Time (Look-Ahead): %s @ %s. Queue size: %zu items.\n", 
                   curDate.c_str(), curTime.c_str(), totalItems);
        }
		*/
        for (size_t i = 0; i < gScheduleList.size(); i++) {
            if (gScheduleList[i].processed) continue;

            std::string normalizedTargetTime = gScheduleList[i].startTime;
            if (normalizedTargetTime.length() == 4 && normalizedTargetTime.find(':') == 1) {
                normalizedTargetTime = "0" + normalizedTargetTime;
            }


            if (gScheduleList[i].processed == 0 && gScheduleList[i].startDate == curDate && normalizedTargetTime == curTime) {    
                databaseUpdated = true;
            
                RecordingConfig* rec = new RecordingConfig();
                rec->channel = gScheduleList[i].channel;
                
                // Add 60 seconds of padding to the recording duration
                int totalDurationSeconds = std::atoi(gScheduleList[i].duration.c_str()) + 60;
                rec->duration = std::to_string(totalDurationSeconds); 
            
                bool tunerFound = false;
                if (!gScheduleList[i].tunerIp.empty()) {
                    rec->ip = gScheduleList[i].tunerIp;
                    tunerFound = true;
                } else {
                    std::vector<std::string> discoveredDevices = DiscoverAllTuners();        
                    if (!discoveredDevices.empty()) {
                        rec->ip = discoveredDevices[0]; 
                        tunerFound = true;
                    } 
                }

                if (tunerFound) {
                    std::string baseDir = gGlobalSaveDirectory;
                    if (!baseDir.empty() && baseDir.back() != '/') {
                        baseDir += "/";
                    }
                    
                    std::string safeTime = gScheduleList[i].startTime;
                    std::replace(safeTime.begin(), safeTime.end(), ':', '-');
            
                    rec->path = baseDir + "DVR_Record_Ch_" + rec->channel + "_" 
                              + gScheduleList[i].startDate + "_" + safeTime + "_"
                              + gScheduleList[i].duration + "s_Padded.ts";
                              
                    // Detach index tracking since we erase the vector item immediately
                    rec->dbIndexPosition = -1; 
            
                    thread_id worker = spawn_thread(BackgroundRecordingWorker, "DVRServiceWorker", B_NORMAL_PRIORITY, rec);
                    if (worker >= B_OK) {
                        resume_thread(worker);
                    } else {
                        delete rec;
                    }
                } else {
                    delete rec; 
                }

                // FIX: Erase item from array immediately so subsequent file reads see it as cleared.
                gScheduleList.erase(gScheduleList.begin() + i);
                
                // FIX: Decrement index counter to avoid skipping the next schedule item in the loop
                i--; 
            }
        }
        gScheduleLocker.Unlock();

        if (databaseUpdated) {
            printf("[SERVER DEBUG] Schedule fired and purged. Updating database changes to disk file...\n");
            SaveSchedulesToDisk();
        }
    }
    printf("[SERVER DEBUG] Scheduler Loop exiting cleanly.\n");
    return 0;
}





class DVRServiceApp : public BApplication {
private:
    thread_id fLoopThread;
public:
    DVRServiceApp() : BApplication("application/x-vnd.haikuhdhomerun-dvr") {}
    
    void ReadyToRun() override {
        LoadSchedulesFromDisk();
        fLoopThread = spawn_thread(ServiceSchedulerLoop, "DVRServiceScheduler", B_LOW_PRIORITY, NULL);
        if (fLoopThread >= B_OK) resume_thread(fLoopThread);
    }
    
    
        void MessageReceived(BMessage* message) override {
        switch (message->what) {

               case 'bFrc': {
                const char* targetChannel = nullptr;
                const char* targetDuration = nullptr;
                
                if (message->FindString("channel", &targetChannel) == B_OK &&
                    message->FindString("duration", &targetDuration) == B_OK) {
                    
                    RecordingConfig* rec = new RecordingConfig();
                    rec->channel = targetChannel;
                    rec->duration = targetDuration;
                    
                    bool tunerFound = false;
                    std::vector<std::string> discoveredDevices = DiscoverAllTuners();
                    
                    if (!discoveredDevices.empty()) {
                        rec->ip = discoveredDevices[0];
                        tunerFound = true;
                    }

                    if (tunerFound) {
                        std::string baseDir = gGlobalSaveDirectory;
                        if (!baseDir.empty() && baseDir.back() != '/') {
                            baseDir += "/";
                        }
                        
                        std::time_t rawTime = std::time(nullptr);
                        std::tm* timeInfo = std::localtime(&rawTime);
                        char timestampBuffer[64];
                        std::strftime(timestampBuffer, sizeof(timestampBuffer), "%Y-%m-%d_%H-%M-%S", timeInfo);

                        rec->path = baseDir + "DVR_Record_Ch_" + rec->channel + "_" 
                                  + timestampBuffer + "_" + rec->duration + "s.ts";
                        
                        rec->dbIndexPosition = -1; 

                        thread_id worker = spawn_thread(BackgroundRecordingWorker, "DVRServiceWorker", B_NORMAL_PRIORITY, rec);
                        if (worker >= B_OK) {
                            resume_thread(worker);
                        } else {
                            delete rec;
                        }
                    } else {
                        delete rec;
                    }
                }
                break;
            }            
            
            default:
                BApplication::MessageReceived(message);
                break;
        }
    }
    
    bool QuitRequested() override {     
	    atomic_set(&gStopService, 1);
	    atomic_set(&gCancelRecording, 1);        
	    return true; 
	}
};


int main() {
    DVRServiceApp app;
    app.Run();
    return 0;
}

