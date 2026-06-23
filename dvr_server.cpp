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
#include <map>
#include <nlohmann/json.hpp>
#include "hdhomerun.h"

const uint32 MSG_ABORT_SPECIFIC_RECORDING = 'absp';



struct ActiveWorkerInfo {
    thread_id threadId;
    int32*    cancellationFlag; 
};

std::map<std::string, ActiveWorkerInfo> gRunningWorkersMap;
BLocker                                 gRunningWorkersLocker("RunningWorkersLock");


using json = nlohmann::json;

const char* kSettingsFilePath = "/boot/home/config/settings/HaikuDVR_schedules.json";

int32 gStopService = 0;
int32 gCancelRecording = 0; 

struct ScheduleItem {
    std::string startDate;
    std::string startTime;
    std::string channel;
    std::string duration;
    std::string showTitle; 
    std::string tunerIp; 
    bool processed;
};


struct RecordingConfig {
    std::string ip;
    std::string channel;
    std::string duration;
    std::string path;
    int dbIndexPosition;
    thread_id workerThread;
};

std::vector<ScheduleItem> gScheduleList;
std::string gGlobalSaveDirectory = "/boot/home";
BLocker gScheduleLocker;
bool gFrontendUpdateNotifications = true;
bool gFrontendDebugEnable = true;
BString gFrontendDefaultPlayer = "hTV";


void SignalExitInterceptor(int signalType) { 
    atomic_set(&gStopService, 1);
    atomic_set(&gCancelRecording, 1);
}


std::vector<std::string> DiscoverAllTuners() {
    std::vector<std::string> tuners;
    struct hdhomerun_discover_device_t result_list[64];
    
    hdhomerun_discover_t* ds = hdhomerun_discover_create(NULL);
    if (ds == NULL) {
        // Log locally or handle allocation failure out to standard error
        fprintf(stderr, "Error: Failed to create hdhomerun discovery instance\n");
        return tuners; // Returns clean empty vector
    }
    
    int count = hdhomerun_discover_find_devices_v2(ds, 0, 
                    HDHOMERUN_DEVICE_TYPE_TUNER, HDHOMERUN_DEVICE_ID_WILDCARD, 
                    result_list, 64);
                    
    for (int i = 0; i < count; i++) {
        uint32_t ip = result_list[i].ip_addr;
        char ip_str[32];
        
        // Convert to standard dot-decimal network notation string
        sprintf(ip_str, "%u.%u.%u.%u", 
            (ip >> 24) & 0xFF, 
            (ip >> 16) & 0xFF, 
            (ip >> 8)  & 0xFF, 
            ip & 0xFF);
            
        tuners.push_back(std::string(ip_str));
    }
    
    hdhomerun_discover_destroy(ds);
    return tuners; // Returns discovered tuners, or empty vector if none found
}


void LoadSchedulesFromDisk() {
    std::ifstream file(kSettingsFilePath);
    if (!file.is_open()) return;

    try {
        json jIn;
        file >> jIn;
        gScheduleLocker.Lock();
        
        if (jIn.is_object()) {
            gGlobalSaveDirectory = jIn.value("save_directory", "/boot/home");
            
            // --- BACKEND FIX: PRESERVE FRONTEND SETTINGS IN RAM ---
            gFrontendUpdateNotifications = jIn.value("show_update_notifications", true);
            gFrontendDebugEnable         = jIn.value("debug_enable", true);
            gFrontendDefaultPlayer       = jIn.value("default_player", "hTV").c_str();

            if (jIn.contains("schedules") && jIn["schedules"].is_array()) {
                gScheduleList.clear();
                for (const auto& entry : jIn["schedules"]) {
                    ScheduleItem item;
                    item.startDate = entry.value("date", "2026-06-23"); // Updated fallback to current date
                    item.startTime = entry.value("time", "12:00");
                    item.channel   = entry.value("channel", "5.1");
                    item.duration  = entry.value("duration", "1800");                    
                    item.tunerIp   = entry.value("tuner_ip", ""); 
                    item.showTitle = entry.value("show_title", "Unknown_Show"); 
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
    
    json jRoot = json::object();
    jRoot["save_directory"] = gGlobalSaveDirectory;
    
    // --- BACKEND FIX: INJECT FRONTEND SETTINGS BACK INTO THE JSON OBJECT ---
    jRoot["show_update_notifications"] = gFrontendUpdateNotifications;
    jRoot["debug_enable"]              = gFrontendDebugEnable;
    jRoot["default_player"]            = gFrontendDefaultPlayer.String();
    
    json jSchedules = json::array();
    for (const auto& item : gScheduleList) {
        jSchedules.push_back({
            {"date", item.startDate},
            {"time", item.startTime},
            {"channel", item.channel},
            {"duration", item.duration},
            {"processed", item.processed},
            {"tuner_ip", item.tunerIp},
            {"show_title", item.showTitle} 
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

int CurlProgressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    int32* cancelRequested = static_cast<int32*>(clientp);
    if (cancelRequested != nullptr && atomic_get(cancelRequested) == 1) {
        return 1; 
    }
    return 0; 
}

int32 BackgroundRecordingWorker(void* data) {
    RecordingConfig* config = static_cast<RecordingConfig*>(data);
    
    int32 cancelFlag = 0;
    
    gRunningWorkersLocker.Lock();
    ActiveWorkerInfo info;
    info.threadId = find_thread(nullptr); 
    info.cancellationFlag = &cancelFlag;
    gRunningWorkersMap[config->path] = info;
    gRunningWorkersLocker.Unlock();

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

            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L); 
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlProgressCallback);
            curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &cancelFlag);

            curl_easy_perform(curl);
            outputFile.close();
        } else {
            printf("[WORKER ERROR] Failed to open target file for writing!\n");
        }
        curl_easy_cleanup(curl);
        
        gScheduleLocker.Lock();
        if (config->dbIndexPosition >= 0 && (size_t)config->dbIndexPosition < gScheduleList.size()) {
            gScheduleList[config->dbIndexPosition].processed = 2; 
        }
        gScheduleLocker.Unlock();
        SaveSchedulesToDisk();
    }

    gRunningWorkersLocker.Lock();
    gRunningWorkersMap.erase(config->path); 
    gRunningWorkersLocker.Unlock();

    delete config;
    return 0;
}


int32 ServiceSchedulerLoop(void* data) {    
    while (atomic_get(&gStopService) == 0) {
        snooze(5000000); 
        LoadSchedulesFromDisk();
        
        std::time_t now = std::time(nullptr);
        std::time_t nextMinTime = now + 60; 
        
        std::tm* localTime = std::localtime(&now);
        std::tm* localNextTime = std::localtime(&nextMinTime);
        
        char dateBuf[32];
        char timeBuf[32];
        char nextTimeBuf[32];
        char nextDateBuf[32];
        
        std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", localTime);
        std::strftime(timeBuf, sizeof(timeBuf), "%H:%M", localTime);
        std::strftime(nextDateBuf, sizeof(nextDateBuf), "%Y-%m-%d", localNextTime);
        std::strftime(nextTimeBuf, sizeof(nextTimeBuf), "%H:%M", localNextTime);
        
        std::string curDate(dateBuf);
        std::string curTime(timeBuf);
        std::string nextDate(nextDateBuf);
        std::string nextTime(nextTimeBuf);
        bool databaseUpdated = false;

        gScheduleLocker.Lock();
        
        for (size_t i = 0; i < gScheduleList.size(); i++) {
            if (gScheduleList[i].processed) continue;

            std::string normalizedTargetTime = gScheduleList[i].startTime;
            if (normalizedTargetTime.length() == 4 && normalizedTargetTime.find(':') == 1) {
                normalizedTargetTime = "0" + normalizedTargetTime;
            }

            bool shouldFire = false;

            if (gScheduleList[i].startDate < curDate) {
                shouldFire = true; 
            } else if (gScheduleList[i].startDate == curDate && normalizedTargetTime <= curTime) {
                shouldFire = true;
            }
            
            else if (gScheduleList[i].startDate == nextDate && normalizedTargetTime == nextTime) {
                shouldFire = true;
            }

            if (gScheduleList[i].processed == 0 && shouldFire) {    
                databaseUpdated = true;
            
                RecordingConfig* rec = new RecordingConfig();
                rec->channel = gScheduleList[i].channel;
                
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
            
                    std::string safeTitle = gScheduleList[i].showTitle;
                    if (safeTitle.empty()) {
                        safeTitle = "Unknown_Show";
                    } else {
                        std::replace(safeTitle.begin(), safeTitle.end(), ' ', '_');
                        std::replace(safeTitle.begin(), safeTitle.end(), '/', '-');
                        std::replace(safeTitle.begin(), safeTitle.end(), '\\', '-');
                        std::replace(safeTitle.begin(), safeTitle.end(), ':', '-');
                        std::replace(safeTitle.begin(), safeTitle.end(), '*', '-');
                        std::replace(safeTitle.begin(), safeTitle.end(), '?', '-');
                        std::replace(safeTitle.begin(), safeTitle.end(), '"', '-');
                        std::replace(safeTitle.begin(), safeTitle.end(), '<', '-');
                        std::replace(safeTitle.begin(), safeTitle.end(), '>', '-');
                        std::replace(safeTitle.begin(), safeTitle.end(), '|', '-');
                    }

                    rec->path = baseDir + "DVR_Record_Ch_" + rec->channel + "_" 
                              + gScheduleList[i].startDate + "_" + safeTime + "_"
                              + safeTitle + "_" 
                              + gScheduleList[i].duration + "s_Padded.ts";
                              
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

                gScheduleList.erase(gScheduleList.begin() + i);
                i--; 
            }
        }
        gScheduleLocker.Unlock();

        if (databaseUpdated) {
            SaveSchedulesToDisk();
        }
    }
    
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
        	
        	
				
	        case MSG_ABORT_SPECIFIC_RECORDING: {
	            // =========================================================================
	            // BACKEND SERVICE: SEARCH THE MAP AND TOGGLE THE CANCELLATION FLAG
	            // =========================================================================
	            const char* targetPath = nullptr;
	            if (message->FindString("file_path", &targetPath) == B_OK && targetPath != nullptr) {
	                std::string pathKey(targetPath);
	                
	                gRunningWorkersLocker.Lock();
	                auto iterator = gRunningWorkersMap.find(pathKey);
	                
	                if (iterator != gRunningWorkersMap.end()) {
	                    if (iterator->second.cancellationFlag != nullptr) {
	                        atomic_set(iterator->second.cancellationFlag, 1);
	                    }
	                    
	                    snooze(100000); 
	                    gRunningWorkersMap.erase(iterator);
	                } else {
	                    printf("[DVR SERVICE ERROR] Path key was NOT found in active worker map!\n");
	                }
	                gRunningWorkersLocker.Unlock();
	            }
	            break;
	        }


               case 'bFrc': {
                const char* targetChannel = nullptr;
                const char* targetDuration = nullptr;
                const char* targetTitle = nullptr; 
                
                if (message->FindString("channel", &targetChannel) == B_OK &&
                    message->FindString("duration", &targetDuration) == B_OK) {
                    
                    std::string safeTitle = "Manual_Record";
                    if (message->FindString("show_title", &targetTitle) == B_OK && targetTitle != nullptr) {
                        safeTitle = targetTitle;
                    }
                    
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

                        time_t now = std::time(nullptr);
                        struct tm* localTimeInfo = std::localtime(&now);
                        
                        char dateBuf[32];
                        char timeBuf[32];
                        std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", localTimeInfo);
                        std::strftime(timeBuf, sizeof(timeBuf), "%H-%M", localTimeInfo); 

                        std::replace(safeTitle.begin(), safeTitle.end(), ' ', '_');
                        std::replace(safeTitle.begin(), safeTitle.end(), '/', '-');
                        std::replace(safeTitle.begin(), safeTitle.end(), '\\', '-');
                        std::replace(safeTitle.begin(), safeTitle.end(), ':', '-');

                        rec->path = baseDir + "DVR_Record_Ch_" + rec->channel + "_" 
                                  + dateBuf + "_" + timeBuf + "_"
                                  + safeTitle + "_"
                                  + rec->duration + "s_Padded.ts";
                                  
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

