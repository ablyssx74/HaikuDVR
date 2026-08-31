/*
 * Copyright 2026, Kris Beazley HaikuDVR@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <Application.h>
#include <OS.h>
#include <SupportDefs.h>
#include <Locker.h>
#include <curl/curl.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <ctime>
#include <signal.h>
#include <unistd.h> 
#include <map>
#include <iomanip>
#include <sstream>
#include <nlohmann/json.hpp>
#include "hdhomerun.h"
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <thread> 
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/types.h>
#include <algorithm>
#include <regex>
#include <cctype>
#include <functional>
#include <sqlite3.h>
#include <cstdlib> 
#include <netinet/tcp.h> 





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
    time_t      epochStart;   
    int64       durationSec;   
    std::string duration;      
    std::string channel;
    std::string tunerIp;
    std::string showTitle;
    std::string showDescription; 
    bool        processed;
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
bool gFrontendDlnaEnable = true;
bool gFrontendDebugEnable = true;
bool gFrontendFullscreenEnable = true;
BString gFrontendDefaultPlayer = "MPV";



std::vector<std::string> DiscoverAllTuners() {
    std::vector<std::string> tuners;
    struct hdhomerun_discover_device_t result_list[64];
    
    hdhomerun_discover_t* ds = hdhomerun_discover_create(NULL);
    if (ds == NULL) {
        if (gFrontendDebugEnable) fprintf(stderr, "Error: Failed to create hdhomerun discovery instance\n");
        return tuners;
    }
    
    int count = hdhomerun_discover_find_devices_v2(ds, 0, 
                    HDHOMERUN_DEVICE_TYPE_TUNER, HDHOMERUN_DEVICE_ID_WILDCARD, 
                    result_list, 64);
                    
    for (int i = 0; i < count; i++) {
        uint32_t ip = result_list[i].ip_addr;
        char ip_str[32];
        
        sprintf(ip_str, "%u.%u.%u.%u", 
            (ip >> 24) & 0xFF, 
            (ip >> 16) & 0xFF, 
            (ip >> 8)  & 0xFF, 
            ip & 0xFF);
            
        tuners.push_back(std::string(ip_str));
    }
    
    hdhomerun_discover_destroy(ds);
    return tuners; 
}



std::string GetHaikuLocalIpAddress() {
    std::string detectedIp = "127.0.0.1"; // Default fallback
    struct ifaddrs* interfaces = nullptr;
    
    if (getifaddrs(&interfaces) == 0) {
        struct ifaddrs* ifa = interfaces;
        while (ifa != nullptr) {
            // Only look for active IPv4 interfaces that are not the local loopback (loop) card
            if (ifa->ifa_addr != nullptr && ifa->ifa_addr->sa_family == AF_INET) {
                std::string interfaceName(ifa->ifa_name);
                
                // Skip loopback (local loop cards)
                if (interfaceName != "loop" && interfaceName != "lo") {
                    char ipBuffer[INET_ADDRSTRLEN] = {0};
                    struct sockaddr_in* socketAddress = (struct sockaddr_in*)ifa->ifa_addr;
                    
                    inet_ntop(AF_INET, &(socketAddress->sin_addr), ipBuffer, INET_ADDRSTRLEN);
                    detectedIp = std::string(ipBuffer);
                    break; // Successfully grabbed your primary network interface IP address
                }
            }
            ifa = ifa->ifa_next;
        }
        freeifaddrs(interfaces);
    }
    return detectedIp;
}




// Unified helper utility to generate absolute epoch time from file strings
static time_t CalculateEpoch(const std::string& dateStr, const std::string& timeStr) {
    std::string fullDateTimeStr = dateStr + " " + timeStr;
    std::tm tm_struct = {};
    std::istringstream ss(fullDateTimeStr);
    ss >> std::get_time(&tm_struct, "%Y-%m-%d %H:%M");
    if (!ss.fail()) {
        // Use standard tm structure field name
        tm_struct.tm_isdst = -1; // Let the system handle DST transitions
        return std::mktime(&tm_struct);
    }
    return 0; 
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
            
            // --- BACKEND PRESERVE FRONTEND SETTINGS IN RAM ---
            gFrontendUpdateNotifications = jIn.value("show_update_notifications", true);
            gFrontendDlnaEnable          = jIn.value("dlna_enable", true);
            gFrontendDebugEnable         = jIn.value("debug_enable", true);
            gFrontendFullscreenEnable	 = jIn.value("enable_fullscreen", true);
            gFrontendDefaultPlayer       = jIn.value("default_player", "mpv").c_str();

            if (jIn.contains("schedules") && jIn["schedules"].is_array()) {
                gScheduleList.clear();
                for (const auto& entry : jIn["schedules"]) {
                    ScheduleItem item;
                    item.startDate    = entry.value("date", "2026-06-23"); 
                    item.startTime    = entry.value("time", "12:00");
                    item.channel      = entry.value("channel", "5.1");
                    item.duration     = entry.value("duration", "1800");                    
                    item.tunerIp      = entry.value("tuner_ip", ""); 
                    item.showTitle    = entry.value("show_title", "Unknown_Show"); 
                    item.showDescription = entry.value("show_description", "No description available."); 
                    item.processed    = entry.value("processed", false);
                    
                    // Added calculation math metrics generation for background workers
                    item.durationSec  = std::atoll(item.duration.c_str());
                    item.epochStart   = CalculateEpoch(item.startDate, item.startTime);

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
    
    // Assign directly since gGlobalSaveDirectory is a standard std::string in the backend
    jRoot["save_directory"] = gGlobalSaveDirectory;
    
    // --- BACKEND INJECT FRONTEND SETTINGS BACK INTO THE JSON OBJECT ---
    jRoot["show_update_notifications"] = gFrontendUpdateNotifications;
    jRoot["dlna_enable"]               = gFrontendDlnaEnable;
    jRoot["debug_enable"]              = gFrontendDebugEnable;
    jRoot["enable_fullscreen"]         = gFrontendFullscreenEnable;
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
            {"show_title", item.showTitle},
            {"show_description", item.showDescription}  
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
           if (gFrontendDebugEnable) printf("[WORKER ERROR] Failed to open target file for writing!\n");
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


// Standard SSDP Configuration Values
#define SSDP_MULTICAST_IP "239.255.255.250"
#define SSDP_PORT 1900

// Thread function signature for background execution matching Haiku standards
static int32 dlna_discovery_worker_thread(void* data);

class DlnasDiscoveryServer {
public:
    thread_id serverThread;
    int       socketFd;
    int       httpPort;
    std::string localIp;

    DlnasDiscoveryServer() : serverThread(-1), socketFd(-1), httpPort(8081), localIp("0.0.0.0") {}

    ~DlnasDiscoveryServer() {
        Stop();
    }

    void Start(const std::string& ipAddress, int port) {
        // Toggle check: Bypass initialization entirely if DLNA is disabled
        if (!gFrontendDlnaEnable) { 
            return; 
        }

        localIp = ipAddress;
        httpPort = port;
        
        // Use Haiku's native multi-threading architecture
        serverThread = spawn_thread(dlna_discovery_worker_thread, "DLNA_SSDP_Engine", B_NORMAL_PRIORITY, this);
        if (serverThread >= 0) {
            resume_thread(serverThread);
            if (gFrontendDebugEnable) std::printf("[DVR BACKEND] DLNA SSDP Discovery service thread spawned successfully.\n");
        } else {
            if (gFrontendDebugEnable) std::printf("[DVR BACKEND ERROR] Failed to spawn DLNA SSDP thread!\n");
        }
    }

    void Stop() {
        if (socketFd >= 0) {
            // Closing the socket breaks recvfrom() and unlocks the worker thread instantly
            close(socketFd);
            socketFd = -1;
        }
        if (serverThread >= 0) {
            status_t exitValue;
            wait_for_thread(serverThread, &exitValue);
            serverThread = -1;
        }
    }
};




// Helper function to dynamically pull the live system IP
static std::string GetLiveSystemIP() {
    std::string detectedIp = "0.0.0.0";
    struct ifaddrs* interfaces = nullptr;
    
    if (getifaddrs(&interfaces) == 0) {
        for (struct ifaddrs* ifa = interfaces; ifa != nullptr; ifa = ifa->ifa_next) {
            if (!ifa->ifa_name || !ifa->ifa_addr || !(ifa->ifa_flags & IFF_UP)) continue;
            
            if (ifa->ifa_addr->sa_family == AF_INET && !(ifa->ifa_flags & IFF_LOOPBACK)) {
                struct sockaddr_in* ethAddr = (struct sockaddr_in*)ifa->ifa_addr;
                char ipBuffer[INET_ADDRSTRLEN];
                if (inet_ntop(AF_INET, &(ethAddr->sin_addr), ipBuffer, INET_ADDRSTRLEN)) {
                    detectedIp = ipBuffer;
                    if (detectedIp != "0.0.0.0" && !detectedIp.empty()) break; 
                }
            }
        }
        freeifaddrs(interfaces);
    }
    return detectedIp;
}

// Standardized utility to turn raw XML/HTML entity codes back into plain text string symbols
static std::string DecodeHtmlEntities(const std::string& input) {
    if (input.empty()) return "";
    
    std::string result = input;
    
    // Create a sequential replace helper structure
    struct EntityPair { std::string encoded; std::string decoded; };
    const EntityPair mappings[] = {
        {"&quot;", "\""},
        {"&amp;",  "&"},
        {"&apos;", "'"},
        {"&#39;",  "'"},
        {"&lt;",   "<"},
        {"&gt;",   ">"}
    };

    for (const auto& pair : mappings) {
        size_t pos = 0;
        while ((pos = result.find(pair.encoded, pos)) != std::string::npos) {
            result.replace(pos, pair.encoded.length(), pair.decoded);
            pos += pair.decoded.length(); // Advance past the decoded text insertion layout
        }
    }
    
    return result;
}



bool IngestMasterXmlToSqlite(const std::string& masterXmlPath) {
    const std::string dbPath = "/boot/home/config/settings/HaikuDVR/guide.db";
    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
        std::printf("[DVR DB ERROR] Failed to open database.\n");
        return false;
    }

    // 1. Establish the database structures and performance options
    sqlite3_exec(db, "PRAGMA synchronous = NORMAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA journal_mode = WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA cache_size = -8000;", nullptr, nullptr, nullptr); 
    
    const char* schema = 
        "CREATE TABLE IF NOT EXISTS channels (xml_id TEXT PRIMARY KEY, lcn TEXT, icon_url TEXT);"
        "CREATE TABLE IF NOT EXISTS programs (channel_id TEXT, title TEXT, desc TEXT, start_epoch INTEGER, end_epoch INTEGER);"
        "CREATE INDEX IF NOT EXISTS idx_channels_lcn_lookup ON channels (lcn, xml_id, icon_url);"
        "CREATE INDEX IF NOT EXISTS idx_programs_timeline_covering ON programs (start_epoch, end_epoch, channel_id, title, [desc]);";
    
    if (sqlite3_exec(db, schema, nullptr, nullptr, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return false;
    }

    // Clear old guide schedules to prepare for fresh data injection
    sqlite3_exec(db, "DELETE FROM programs;", nullptr, nullptr, nullptr);

    std::ifstream masterStream(masterXmlPath.c_str());
    if (!masterStream.is_open()) {
        sqlite3_close(db);
        return false;
    }

    auto parseXmlTimeToEpoch = [](const std::string& rawXmlTime) -> std::time_t {
        if (rawXmlTime.length() < 14) return 0;
        int y = 0, mo = 0, d = 0, h = 0, m = 0, s = 0;
        std::sscanf(rawXmlTime.substr(0, 4).c_str(), "%4d", &y);
        std::sscanf(rawXmlTime.substr(4, 2).c_str(), "%2d", &mo);
        std::sscanf(rawXmlTime.substr(6, 2).c_str(), "%2d", &d);
        std::sscanf(rawXmlTime.substr(8, 2).c_str(), "%2d", &h);
        std::sscanf(rawXmlTime.substr(10, 2).c_str(), "%2d", &m);
        std::sscanf(rawXmlTime.substr(12, 2).c_str(), "%2d", &s);

        std::tm tmTime = {0};
        tmTime.tm_year  = y - 1900;
        tmTime.tm_mon   = mo - 1;
        tmTime.tm_mday  = d;
        tmTime.tm_hour  = h;
        tmTime.tm_min   = m;
        tmTime.tm_sec   = s;
        tmTime.tm_isdst = -1;

        std::time_t utcEpoch = timegm(&tmTime);
        if (utcEpoch == (std::time_t)-1) return 0;
        
        long providerOffsetSeconds = 0;
        size_t spacePos = rawXmlTime.find(' ');
        if (spacePos != std::string::npos && spacePos + 5 <= rawXmlTime.length()) {
            int sign = (rawXmlTime[spacePos + 1] == '-') ? -1 : 1;
            int oh = 0, om = 0;
            std::sscanf(rawXmlTime.substr(spacePos + 2, 2).c_str(), "%2d", &oh);
            std::sscanf(rawXmlTime.substr(spacePos + 4, 2).c_str(), "%2d", &om);
            providerOffsetSeconds = sign * ((oh * 3600) + (om * 60));
        }
        return utcEpoch - providerOffsetSeconds;
    };

    // Prepare separate compiled statements for independent, high-performance insertion/updates
    sqlite3_stmt* chanInitStmt = nullptr;
    sqlite3_prepare_v2(db, "INSERT OR IGNORE INTO channels (xml_id) VALUES (?);", -1, &chanInitStmt, nullptr);

    sqlite3_stmt* chanLcnStmt = nullptr;
    sqlite3_prepare_v2(db, "UPDATE channels SET lcn = ? WHERE xml_id = ?;", -1, &chanLcnStmt, nullptr);

    sqlite3_stmt* chanIconStmt = nullptr;
    sqlite3_prepare_v2(db, "UPDATE channels SET icon_url = ? WHERE xml_id = ?;", -1, &chanIconStmt, nullptr);

    sqlite3_stmt* progStmt = nullptr;
    sqlite3_prepare_v2(db, "INSERT INTO programs VALUES (?, ?, ?, ?, ?);", -1, &progStmt, nullptr);

    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    std::string line;
    std::string currentChannelId = "";
    std::string progChanId = "", progStartRaw = "", progEndRaw = "", titleText = "", descText = "";
    bool insideProgramme = false;
    std::time_t current_time = std::time(nullptr);

    while (std::getline(masterStream, line)) {
        // Parse channels
        size_t chanPos = line.find("<channel id=\"");
        if (chanPos != std::string::npos) {
            size_t startIdx = chanPos + 13;
            size_t endIdx = line.find("\"", startIdx);
            if (endIdx != std::string::npos) {
                currentChannelId = line.substr(startIdx, endIdx - startIdx);
                
                // Initialize the channel entry safely without touching old records
                sqlite3_bind_text(chanInitStmt, 1, currentChannelId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(chanInitStmt);
                sqlite3_reset(chanInitStmt);
            }
            continue;
        }

        size_t lcnPos = line.find("<lcn>");
        if (lcnPos != std::string::npos && !currentChannelId.empty()) {
            size_t endIdx = line.find("</lcn>");
            if (endIdx != std::string::npos) {
                std::string lcnVal = line.substr(lcnPos + 5, endIdx - (lcnPos + 5));
                
                // Update LCN cleanly
                sqlite3_bind_text(chanLcnStmt, 1, lcnVal.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(chanLcnStmt, 2, currentChannelId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(chanLcnStmt);
                sqlite3_reset(chanLcnStmt);
            }
            continue;
        }

        size_t iconPos = line.find("<icon src=\"");
        if (iconPos != std::string::npos && !currentChannelId.empty()) {
            size_t endIdx = line.find("\"", iconPos + 11);
            if (endIdx != std::string::npos) {
                std::string iconUrl = line.substr(iconPos + 11, endIdx - (iconPos + 11));
                
                // Update Icon URL cleanly without disturbing the LCN value
                sqlite3_bind_text(chanIconStmt, 1, iconUrl.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(chanIconStmt, 2, currentChannelId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(chanIconStmt);
                sqlite3_reset(chanIconStmt);
            }
            currentChannelId = "";
            continue;
        }

        // =========================================================================
        // PRESERVED SECURE PROGRAM NODE PARSING MATRIX
        // =========================================================================
        size_t progPos = line.find("<programme start=\"");
        if (progPos != std::string::npos) {
            insideProgramme = true;
            titleText = ""; descText = "";
            
            size_t sEnd = line.find("\"", progPos + 18);
            if (sEnd != std::string::npos) progStartRaw = line.substr(progPos + 18, sEnd - (progPos + 18));

            size_t stopPos = line.find("stop=\"");
            if (stopPos != std::string::npos) {
                size_t eEnd = line.find("\"", stopPos + 6);
                if (eEnd != std::string::npos) progEndRaw = line.substr(stopPos + 6, eEnd - (stopPos + 6));
            }

            size_t chanAttrPos = line.find("channel=\"");
            if (chanAttrPos != std::string::npos) {
                size_t cEnd = line.find("\"", chanAttrPos + 9);
                if (cEnd != std::string::npos) progChanId = line.substr(chanAttrPos + 9, cEnd - (chanAttrPos + 9));
            }
            continue;
        }

        if (insideProgramme) {
            size_t titlePos = line.find("<title");
            if (titlePos != std::string::npos) {
                size_t valStart = line.find(">", titlePos) + 1;
                size_t valEnd = line.find("</title>", valStart);
                if (valEnd != std::string::npos) titleText = line.substr(valStart, valEnd - valStart);
            }
            size_t descPos = line.find("<desc");
            if (descPos != std::string::npos) {
                size_t valStart = line.find(">", descPos) + 1;
                size_t valEnd = line.find("</desc>", valStart);
                if (valEnd != std::string::npos) descText = line.substr(valStart, valEnd - valStart);
            }

            if (line.find("</programme>") != std::string::npos) {
                insideProgramme = false;
                std::time_t startEp = parseXmlTimeToEpoch(progStartRaw);
                std::time_t endEp   = parseXmlTimeToEpoch(progEndRaw);

                if (endEp < startEp) endEp += 86400; // Account for overnight date boundary wraps

                // THE FILTER PASS: If the show's end time is in the past, skip SQLite execution
                if (endEp < current_time) {
                    continue; 
                }

                if (!progChanId.empty() && !titleText.empty()) {
                    sqlite3_bind_text(progStmt, 1, progChanId.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(progStmt, 2, titleText.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(progStmt, 3, descText.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int64(progStmt, 4, static_cast<sqlite3_int64>(startEp));
                    sqlite3_bind_int64(progStmt, 5, static_cast<sqlite3_int64>(endEp));
                    sqlite3_step(progStmt);
                    sqlite3_reset(progStmt);
                }
            }
        }
    }

    // Close transaction and finalize all prepared statements
    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    
    // Finalize all three distinct channel statement resources
    sqlite3_finalize(chanInitStmt);
    sqlite3_finalize(chanLcnStmt);
    sqlite3_finalize(chanIconStmt);
    sqlite3_finalize(progStmt);

    // Run internal structural analysis optimization before releasing file lock
    sqlite3_exec(db, "PRAGMA optimize;", nullptr, nullptr, nullptr);

    sqlite3_close(db);
    masterStream.close();
    return true;
}







static int32 dlna_discovery_worker_thread(void* data) {
    DlnasDiscoveryServer* server = static_cast<DlnasDiscoveryServer*>(data);
    if (!server) return B_BAD_VALUE;

    char buffer[4096];
    struct sockaddr_in clientAddr{};
    socklen_t addrLen = sizeof(clientAddr);

    if (gFrontendDebugEnable) std::printf("[DVR BACKEND] SSDP Engine active with automatic healing pulse loop.\n");

    // Monitor application state flag and the global DLNA enable configuration toggle continuously
    while (atomic_get(&gStopService) == 0 && gFrontendDlnaEnable) {
        
        // 1. DYNAMIC SOCKET MONITOR: If socket is uninitialized or dropped, try to build it
        if (server->socketFd < 0) {
            std::string liveIp = GetLiveSystemIP();
            
            // If the system hasn't obtained an IP address yet, pulse wait and retry
            if (liveIp == "0.0.0.0" || liveIp.empty()) {
                // Sleep for 5 seconds checking for shutdown requests and config toggles every 500ms
                for (int i = 0; i < 10 && atomic_get(&gStopService) == 0 && gFrontendDlnaEnable; i++) {
                    usleep(500000);
                }
                continue; // Re-evaluate loop
            }

            // Lock in the live IP
            server->localIp = liveIp;
            if (gFrontendDebugEnable) std::printf("[DVR BACKEND] Network link validated at %s. Binding SSDP engine...\n", server->localIp.c_str());

            // Try creating the UDP socket
            server->socketFd = socket(AF_INET, SOCK_DGRAM, 0);
            if (server->socketFd < 0) {
                usleep(5000000); // Wait 5 seconds on fatal socket allocation error
                continue;
            }

            // Set socket options for immediate re-use
            int reuse = 1;
            if (setsockopt(server->socketFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
                close(server->socketFd);
                server->socketFd = -1;
                continue;
            }

            // Bind explicitly to port 1900
            struct sockaddr_in localAddr{};
            localAddr.sin_family = AF_INET;
            localAddr.sin_port = htons(SSDP_PORT);
            localAddr.sin_addr.s_addr = INADDR_ANY; 

            if (bind(server->socketFd, (struct sockaddr*)&localAddr, sizeof(localAddr)) < 0) {
                if (gFrontendDebugEnable) std::printf("[DLNA ERROR] SSDP Bind failed on port 1900. Retrying in 5s...\n");
                close(server->socketFd);
                server->socketFd = -1;
                // Sleep for 5 seconds checking for shutdown or config toggle requests
                for (int i = 0; i < 10 && atomic_get(&gStopService) == 0 && gFrontendDlnaEnable; i++) usleep(500000);
                continue;
            }

            // JOIN the Multicast Group locked to our newly confirmed live system IP
            struct ip_mreq group{};
            group.imr_multiaddr.s_addr = inet_addr(SSDP_MULTICAST_IP);
            group.imr_interface.s_addr = inet_addr(server->localIp.c_str()); 
            
            if (setsockopt(server->socketFd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &group, sizeof(group)) < 0) {
                if (gFrontendDebugEnable) std::printf("[DLNA ERROR] Joining multicast group failed on interface %s\n", server->localIp.c_str());
                close(server->socketFd);
                server->socketFd = -1;
                for (int i = 0; i < 10 && atomic_get(&gStopService) == 0 && gFrontendDlnaEnable; i++) usleep(500000);
                continue;
            }

            // Set a non-blocking timeout of 5 seconds on the socket read operation.
            // This prevents the thread from blocking infinitely on recvfrom and allows the pulse to check its health.
            struct timeval tv;
            tv.tv_sec = 5;
            tv.tv_usec = 0;
            setsockopt(server->socketFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            
            if (gFrontendDebugEnable) std::printf("[DVR BACKEND] SSDP Engine successfully online and listening.\n");
        }

        // 2. NETWORK PACKET RECEIVE
        std::memset(buffer, 0, sizeof(buffer));
        addrLen = sizeof(clientAddr);
        ssize_t bytesRead = recvfrom(server->socketFd, buffer, sizeof(buffer) - 1, 0, 
                                     (struct sockaddr*)&clientAddr, &addrLen);
        
        if (bytesRead < 0) {
            // FIX: Include ETIMEDOUT for Haiku OS compliance
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT) {
                // Heartbeat Pulse Check: Confirm that our bound interface IP hasn't shifted underneath us
                std::string currentCheckIP = GetLiveSystemIP();
                if (currentCheckIP != server->localIp) {
                    if (gFrontendDebugEnable) std::printf("[DVR BACKEND WARNING] System IP change or interface reset detected! Healing socket...\n");
                    close(server->socketFd);
                    server->socketFd = -1; // Forces the top of the loop to rebuild everything next cycle
                }
                continue;
            }
            
            // Socket was explicitly closed by Stop() or dropped catastrophically, or toggled off mid-flight
            if (atomic_get(&gStopService) != 0 || !gFrontendDlnaEnable) break;
            
            close(server->socketFd);
            server->socketFd = -1;
            continue;
        }


        // 3. PROCESS SSDP MATCHES
        std::string packetStr(buffer);
        if (packetStr.find("M-SEARCH") != std::string::npos) {
            if (packetStr.find("ssdp:all") != std::string::npos || 
                packetStr.find("urn:schemas-upnp-org:device:MediaServer:1") != std::string::npos) {
                
                // Automatically determine the correct USN string at compile time
                #if defined(__LP64__) || defined(_LP64) || defined(__x86_64__)
                    // 64-bit Architecture
                    const char* usnStr = "USN: uuid:fe80::haiku:dvr-64bit::urn:schemas-upnp-org:device:MediaServer:1\r\n";
                #else
                    // 32-bit Architecture (fallback)
                    const char* usnStr = "USN: uuid:fe80::haiku:dvr-32bit::urn:schemas-upnp-org:device:MediaServer:1\r\n";
                #endif

                char response[1024];
                std::snprintf(response, sizeof(response),
                    "HTTP/1.1 200 OK\r\n"
                    "CACHE-CONTROL: max-age=1800\r\n"
                    "LOCATION: http://%s:%d/description.xml\r\n"
                    "ST: urn:schemas-upnp-org:device:MediaServer:1\r\n"
                    "%s" // Injects the architecture-specific USN string dynamically
                    "EXT:\r\n"
                    "SERVER: HaikuOS HaikuDVR-MediaServer/1.0\r\n"
                    "\r\n", 
                    server->localIp.c_str(), server->httpPort, usnStr
                );

                sendto(server->socketFd, response, std::strlen(response), 0, 
                       (struct sockaddr*)&clientAddr, addrLen);
            }
        }

    }

    // Clean final thread drop
    if (server->socketFd >= 0) {
        close(server->socketFd);
        server->socketFd = -1;
    }
    if (gFrontendDebugEnable) std::printf("[DVR BACKEND] SSDP Engine thread exiting cleanly.\n");
    return B_OK;
}



class DlnasHttpStreamingServer {
public:
    thread_id serverThread;
    int       listenFd;
    int       port;
    std::string rootDir;
	std::string localIp; 
	
    DlnasHttpStreamingServer() : serverThread(-1), listenFd(-1), port(8081) {}
    






	// Endpoint: GET /api/recordings/play?file=FILENAME — Smart local/remote hybrid playback engine
	void HandlePlayRecording(int clientFd, const std::string& requestStr) {
	    size_t queryPos = requestStr.find("/api/recordings/play?file=");
	    if (queryPos == std::string::npos) {
	        SendJsonResponse(clientFd, 400, "Bad Request", "{\"error\":\"Missing file parameter\"}");
	        return;
	    }
	
	    size_t endPos = requestStr.find(" ", queryPos);
	    std::string filename = requestStr.substr(queryPos + 26, endPos - (queryPos + 26));
	
	    gScheduleLocker.Lock();
	    std::string baseDir = gGlobalSaveDirectory;
	    gScheduleLocker.Unlock();
	
	    std::string fullPath = baseDir + "/" + filename;
	
	    // Security verify: Block file access out-of-bounds traversal
	    if (!IsPathSafeAndContained(fullPath, baseDir) || access(fullPath.c_str(), F_OK) != 0) {
	        SendJsonResponse(clientFd, 404, "Not Found", "{\"error\":\"Recording file not found or inaccessible\"}");
	        return;
	    }
	
	    // 1. Identify Client and Server IPs to evaluate boundaries
	    std::string clientIpStr = "127.0.0.1";
	    struct sockaddr_storage peerAddr;
	    socklen_t peerAddrLen = sizeof(peerAddr);
	    if (getpeername(clientFd, (struct sockaddr*)&peerAddr, &peerAddrLen) == 0) {
	        if (peerAddr.ss_family == AF_INET) {
	            struct sockaddr_in* s = (struct sockaddr_in*)&peerAddr;
	            char ipBuffer[INET_ADDRSTRLEN];
	            inet_ntop(AF_INET, &(s->sin_addr), ipBuffer, INET_ADDRSTRLEN);
	            clientIpStr = ipBuffer;
	        }
	    }
	
	    std::string serverIpStr = "";
	    size_t hostPos = requestStr.find("Host: ");
	    if (hostPos != std::string::npos) {
	        size_t valStart = hostPos + 6;
	        size_t valEnd = requestStr.find("\r\n", valStart);
	        if (valEnd != std::string::npos) {
	            std::string fullHost = requestStr.substr(valStart, valEnd - valStart);
	            size_t colonIndex = fullHost.find(":");
	            serverIpStr = (colonIndex != std::string::npos) ? fullHost.substr(0, colonIndex) : fullHost;
	        }
	    }
	
	    bool isLocalClient = (clientIpStr == "127.0.0.1" || clientIpStr == "localhost" || clientIpStr == serverIpStr);
	    std::string streamingUrl = "http://" + (serverIpStr.empty() ? GetLiveHttpSystemIP() : serverIpStr) + ":" + std::to_string(port) + "/video/" + filename;
	
	    if (isLocalClient) {
	        // =========================================================================
	        // LOCAL WORKSPACE WORKSTATION MODE: Native host desktop process fork 
	        // =========================================================================
	        if (gFrontendDebugEnable) {
	            std::printf("[LIBRARY LAUNCHER] Local environment matched! Spinning native desktop playback process context.\n");
	            std::fflush(stdout);
	        }
	
	        const char* binaryPath = "/boot/system/bin/mpv";
	        if (access("/boot/system/bin/hTV", F_OK) == 0) {
	            binaryPath = "/boot/system/bin/hTV";
	        } else if (access("/boot/system/bin/vlc", F_OK) == 0) {
	            binaryPath = "/boot/system/bin/vlc";
	        }
	
	        pid_t processId = fork();
	        if (processId == 0) {
	            char* playerArgs[3];
	            playerArgs[0] = (char*)binaryPath;
	            playerArgs[1] = (char*)fullPath.c_str(); // Pass local file directly for zero-latency local IO play
	            playerArgs[2] = nullptr; 
	            
	            execv(playerArgs[0], playerArgs);
	            _exit(1); 
	        }
	
	        SendJsonResponse(clientFd, 200, "OK", "{\"status\":\"success\",\"mode\":\"local_launch\"}");
	    } else {
	        // =========================================================================
	        // REMOTE NETWORK CLIENT MODE: Package dynamic streaming .pls playlist file
	        // =========================================================================
	        if (gFrontendDebugEnable) {
	            std::printf("[LIBRARY LAUNCHER] Remote environment context matched. Sending PLS video playlist stream back.\n");
	            std::fflush(stdout);
	        }
	
	        std::string plsContent = "[playlist]\r\n";
	        plsContent += "NumberOfEntries=1\r\n";
	        plsContent += "File1=" + streamingUrl + "\r\n";
	        plsContent += "Title1=" + filename + "\r\n";
	        plsContent += "Length1=-1\r\n";
	        plsContent += "Version=2\r\n";
	
	        std::string responseHeaders = 
	            "HTTP/1.1 200 OK\r\n"
	            "Server: HaikuOS HaikuDVR-MediaServer/1.0\r\n"
	            "Content-Type: audio/x-scpls\r\n" 
	            "Content-Disposition: attachment; filename=\"" + filename + ".pls\"\r\n"
	            "Content-Length: " + std::to_string(plsContent.length()) + "\r\n"
	            "Connection: close\r\n\r\n";
	
	        send(clientFd, responseHeaders.c_str(), responseHeaders.length(), 0);
	        send(clientFd, plsContent.c_str(), plsContent.length(), 0);
	        close(clientFd);
	    }
	}


	// Endpoint: GET /api/recordings — Dynamically scans disk and injects active recording status flags
	void HandleGetRecordings(int clientFd) {
	    gScheduleLocker.Lock();
	    std::string baseDir = gGlobalSaveDirectory;
	    gScheduleLocker.Unlock();
	
	    json jList = json::array();
	    DIR* dir = opendir(baseDir.c_str());
	    if (dir == nullptr) {
	        SendJsonResponse(clientFd, 500, "Internal Server Error", "{\"error\":\"Cannot open save directory\"}");
	        return;
	    }
	
	    struct dirent* entry;
	    while ((entry = readdir(dir)) != nullptr) {
	        std::string fileName(entry->d_name);
	        
	        // Only catalog our Transport Stream files
	        if (fileName.length() > 3 && fileName.substr(fileName.length() - 3) == ".ts") {
	            std::string fullPath = baseDir + "/" + fileName;
	            
	            struct stat fileStat;
	            if (stat(fullPath.c_str(), &fileStat) == 0) {
	                double fileSizeMB = static_cast<double>(fileStat.st_size) / (1024.0 * 1024.0);
	                
	                // Parse out Title, Date, and Channel tokens
	                std::string title = fileName;
	                std::string dateStr = "Unknown Date";
	                std::string channelStr = "Unknown Ch";
	                
	                size_t lastDot = fileName.find_last_of(".");
	                std::string nameWithoutExt = fileName.substr(0, lastDot);
	                
	                std::vector<std::string> tokens;
	                std::stringstream ss(nameWithoutExt);
	                std::string token;
	                while (std::getline(ss, token, '_')) {
	                    tokens.push_back(token);
	                }
	                
	                if (tokens.size() >= 3) {
	                    channelStr = tokens.back();
	                    tokens.pop_back();
	                    dateStr = tokens.back();
	                    tokens.pop_back();
	                    
	                    title = "";
	                    for (size_t i = 0; i < tokens.size(); i++) {
	                        title += tokens[i] + (i + 1 < tokens.size() ? " " : "");
	                    }
	                }
	
	                // --- NEW: CROSS-REFERENCE ACTIVE WORKERS ---
	                bool isCurrentlyRecording = false;
	                gRunningWorkersLocker.Lock();
	                if (gRunningWorkersMap.find(fullPath) != gRunningWorkersMap.end()) {
	                    isCurrentlyRecording = true;
	                }
	                gRunningWorkersLocker.Unlock();
	
	                jList.push_back({
	                    {"filename", fileName},
	                    {"title", title},
	                    {"date", dateStr},
	                    {"channel", channelStr},
	                    {"size_mb", MathRoundToTwoDecimals(fileSizeMB)},
	                    {"modified", fileStat.st_mtime},
	                    {"is_recording", isCurrentlyRecording} // <-- Injected active flag status hook
	                });
	            }
	        }
	    }
	    closedir(dir);
	
	    // Sort recordings by newest timestamp first
	    std::sort(jList.begin(), jList.end(), [](const json& a, const json& b) {
	        return a.value("modified", 0LL) > b.value("modified", 0LL);
	    });
	
	    SendJsonResponse(clientFd, 200, "OK", jList.dump());
	}



	// Small helper utility for size formatting string presentation
	double MathRoundToTwoDecimals(double val) {
	    return std::round(val * 100.0) / 100.0;
	}

 

	// Helper utility to grab the actual system IP address
	static std::string GetLiveHttpSystemIP() {
	    std::string detectedIp = "0.0.0.0";
	    struct ifaddrs* interfaces = nullptr;
	    
	    if (getifaddrs(&interfaces) == 0) {
	        for (struct ifaddrs* ifa = interfaces; ifa != nullptr; ifa = ifa->ifa_next) {
	            if (!ifa->ifa_name || !ifa->ifa_addr || !(ifa->ifa_flags & IFF_UP)) continue;
	            
	            if (ifa->ifa_addr->sa_family == AF_INET && !(ifa->ifa_flags & IFF_LOOPBACK)) {
	                struct sockaddr_in* ethAddr = (struct sockaddr_in*)ifa->ifa_addr;
	                char ipBuffer[INET_ADDRSTRLEN];
	                if (inet_ntop(AF_INET, &(ethAddr->sin_addr), ipBuffer, INET_ADDRSTRLEN)) {
	                    detectedIp = ipBuffer;
	                    if (detectedIp != "0.0.0.0" && !detectedIp.empty()) break; 
	                }
	            }
	        }
	        freeifaddrs(interfaces);
	    }
	    return detectedIp;
	}

	static int32 HttpWorkerLoop(void* data) {
	    DlnasHttpStreamingServer* self = static_cast<DlnasHttpStreamingServer*>(data);
	    if (!self) return B_BAD_VALUE;
	
	    // UPDATE: Fast-abort if DLNA was disabled before the thread finished spawning
	    if (!gFrontendDlnaEnable) {
	        return B_OK;
	    }
	
	    if (gFrontendDebugEnable) {
	    	std::printf("[HTTP DEBUG] Creating master TCP stream socket...\n");
	    	std::fflush(stdout);
	    }
	    
	    self->listenFd = socket(AF_INET, SOCK_STREAM, 0);
	    if (self->listenFd < 0) {
	    		if (gFrontendDebugEnable) {
	        		std::printf("[HTTP DEBUG ERROR] Failed to create socket: %s\n", strerror(errno));
	        		std::fflush(stdout);
	    		}
	        return B_ERROR;
	    }
	
	    int reuse = 1;
	    setsockopt(self->listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	
	    // UPDATE: Set a non-blocking timeout of 5 seconds on the master stream socket.
	    // This lets accept() periodically wake up to check gStopService and gFrontendDlnaEnable.
	    struct timeval tv;
	    tv.tv_sec = 5;
	    tv.tv_usec = 0;
	    setsockopt(self->listenFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	
	    struct sockaddr_in srvAddr{};
	    srvAddr.sin_family = AF_INET;
	    srvAddr.sin_port = htons(self->port);
	    srvAddr.sin_addr.s_addr = INADDR_ANY; // Listening on all interfaces is correct
		if (gFrontendDebugEnable) {
	    	std::printf("[HTTP DEBUG] Attempting to bind TCP server to port %d...\n", self->port);
	    	std::fflush(stdout);
		}
	    if (bind(self->listenFd, (struct sockaddr*)&srvAddr, sizeof(srvAddr)) < 0) {
	    	if (gFrontendDebugEnable) {
	        	std::printf("[HTTP DEBUG ERROR] Bind to port %d failed: %s\n", self->port, strerror(errno));
	        	std::fflush(stdout);
	    	}
	        close(self->listenFd);
	        self->listenFd = -1;
	        return B_ERROR;
	    }
	    
	    listen(self->listenFd, 10);
	    if (gFrontendDebugEnable) {
	    	std::printf("[HTTP DEBUG] Server socket bound! Actively listening for TV requests...\n");
	    	std::fflush(stdout);
	    }

	    // --- LIVE IP HEALING AND ACQUISITION ---
	    // If we booted with a stale or blank IP address configuration, poll until one arrives
	    // UPDATE: Now aborts immediately if DLNA is toggled off
	    while ((self->localIp == "0.0.0.0" || self->localIp.empty()) && atomic_get(&gStopService) == 0 && gFrontendDlnaEnable) {
	        std::string checkedIp = GetLiveHttpSystemIP();
	        if (checkedIp != "0.0.0.0" && !checkedIp.empty()) {
	            self->localIp = checkedIp;
	            if (gFrontendDebugEnable) {
	            	std::printf("[HTTP DEBUG] Dynamic IP discovered and assigned: %s\n", self->localIp.c_str());
	            	std::fflush(stdout);
	            }
	            break;
	        }
	        
	        // Check for application shutdown or toggle requests while waiting for the network links
	        for (int i = 0; i < 4 && atomic_get(&gStopService) == 0 && gFrontendDlnaEnable; i++) {
	            usleep(500000); // 2-second polling increments split up safely
	        }
	    }
	    // --- END OF IP HEALING ---
	
	    // UPDATE: Master loop now continuously monitors the DLNA configuration flag
	    while (atomic_get(&gStopService) == 0 && gFrontendDlnaEnable) {
	        struct sockaddr_in cliAddr{};
	        socklen_t cliLen = sizeof(cliAddr);
	        
	        // This call will unblock every 5 seconds (due to our previously set SO_RCVTIMEO) 
	        // to re-evaluate the while loop conditions (shutdown or feature toggle off).
	        int clientFd = accept(self->listenFd, (struct sockaddr*)&cliAddr, &cliLen);
	        if (clientFd < 0) {
	            // FIX: Include ETIMEDOUT for Haiku OS compliance
	            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT) {
	                continue;
	            }
	            
	            // Check if we dropped out due to system shutdown or feature disablement
	            if (atomic_get(&gStopService) != 0 || !gFrontendDlnaEnable) break;
	            
	            if (gFrontendDebugEnable) {
	            	std::printf("[HTTP DEBUG WARNING] Accept connection failed: %s\n", strerror(errno));
	            	std::fflush(stdout);
	            }
	            continue;
	        }
	
	
	        // Heartbeat verification: If the system IP shifted, dynamically re-update it on the fly
	        std::string validationIp = GetLiveHttpSystemIP();
	        if (validationIp != "0.0.0.0" && validationIp != self->localIp) {
	            self->localIp = validationIp;
	            if (gFrontendDebugEnable) {
	            	std::printf("[HTTP DEBUG] Network interface modified. Synchronizing server tracking to: %s\n", self->localIp.c_str());
	            	std::fflush(stdout);
	            }
	        }
	
	        char clientIpStr[INET_ADDRSTRLEN] = {0};
	        inet_ntop(AF_INET, &(cliAddr.sin_addr), clientIpStr, INET_ADDRSTRLEN);
	        if (gFrontendDebugEnable) {
	        	std::printf("\n--------------------------------------------------\n");
	        	std::printf("[HTTP DEBUG] TCP Handshake SUCCESS! Connected client IP: %s\n", clientIpStr);
	        	std::printf("[HTTP DEBUG] Spawning background handler thread...\n");
	        	std::fflush(stdout);
	        }
	
	        std::thread handler([self, clientFd, clientIpStr]() {
	        	if (gFrontendDebugEnable) {
	            	std::printf("[HTTP THREAD] Thread started for client: %s\n", clientIpStr);
	            	std::fflush(stdout);
	        	}
	
	            std::string req;
	            char chunk[2048];
	            ssize_t bytesRecv = 0;
	            int totalBytes = 0;

                // =========================================================================
                // HIGH-PERFORMANCE UNIFIED REQUEST STREAM READER
                // =========================================================================
                // Master socket reader runs independent of the DLNA configuration flag
                while (atomic_get(&gStopService) == 0) {
                    std::memset(chunk, 0, sizeof(chunk));
                    bytesRecv = recv(clientFd, chunk, sizeof(chunk) - 1, 0);
                    
                    if (bytesRecv <= 0) break; // Client gracefully finished or disconnected

                    req.append(chunk, bytesRecv);
                    totalBytes += bytesRecv;

                    // Standard SOAP envelope guard for legacy DLNA controllers
                    if (req.find("</s:Envelope>") != std::string::npos || 
                        req.find("</SOAP-ENV:Envelope>") != std::string::npos) {
                        break;
                    }
                    
                    // IF GET ROUTE: Instantly short-circuit read because GET routes have zero payload text body
                    if (req.find("GET ") == 0 && req.find("\r\n\r\n") != std::string::npos) {
                        break;
                    }

                    // IF POST ROUTE: Ensure we read past the headers and pull the full Content-Length data body
                    if (req.find("POST ") == 0 && req.find("\r\n\r\n") != std::string::npos) {
                        size_t clPos = req.find("Content-Length: ");
                        if (clPos != std::string::npos) {
                            size_t valueStart = clPos + 16;
                            size_t valueEnd = req.find("\r\n", valueStart);
                            if (valueEnd != std::string::npos) {
                                int expectedBodyLength = std::atoi(req.substr(valueStart, valueEnd - valueStart).c_str());
                                size_t headerEndPos = req.find("\r\n\r\n");
                                size_t currentBodyLength = req.length() - (headerEndPos + 4);
                                
                                // Break only when the entire JSON body payload has been pulled from the socket buffer
                                if (currentBodyLength >= (size_t)expectedBodyLength) {
                                    break;
                                }
                            }
                        }
                    }
                    
                    if (totalBytes > 32768) break; // Defensive buffer overrun safeguard
                }
                
                if (gFrontendDebugEnable) {
                    std::printf("[HTTP THREAD] Network stream read complete. Total payload size: %d bytes\n", totalBytes);
                    std::fflush(stdout);
                }

                if (totalBytes <= 0) {
                    close(clientFd);
                    return;
                }
                
                size_t firstNewLine = req.find("\r\n");
                std::string firstLine = (firstNewLine != std::string::npos) ? req.substr(0, firstNewLine) : req;
                
                if (gFrontendDebugEnable) {
                    std::printf("[HTTP THREAD] Request Header Processing: \"%s\"\n", firstLine.c_str());
                    std::fflush(stdout);
                }

                // =========================================================================
	            // CLEANED WEB DASHBOARD REMOTE CONTROL ROUTING MATRIX
	            // =========================================================================
	            
	            // --- VISUAL USER WEB INTERFACE CONTROL DASHBOARD PANEL ---
	            if (req.find("GET / ") != std::string::npos || req.find("GET /index.html") != std::string::npos) {
	                if (gFrontendDebugEnable) std::printf("[HTTP DASHBOARD] Serving Admin Dashboard View\n");
	                self->ServeAdminDashboard(clientFd);
	            }         
	            // --- CUSTOM INTEGRATED DESKTOP REMOTE PLAYER ENDPOINT ---
	            else if (req.find("GET /api/desktop/play?ch=") != std::string::npos) {
	                if (gFrontendDebugEnable) std::printf("[HTTP API] Route matched: GET /api/desktop/play\n");
	                self->HandleDesktopAppLaunch(clientFd, req);
	            }
	            else if (req.find("GET /api/recordings/play?file=") != std::string::npos) {
				    if (gFrontendDebugEnable) std::printf("[HTTP ADMIN API] Route matched: GET /api/recordings/play\n");
				    self->HandlePlayRecording(clientFd, req);				
				}
	            else if (req.find("GET /api/recordings") != std::string::npos) {
				    if (gFrontendDebugEnable) std::printf("[HTTP ADMIN API] Route matched: GET /api/recordings\n");
				    self->HandleGetRecordings(clientFd);
				}

	            // --- CUSTOM INTEGRATED ADMIN API ENDPOINTS ---
	            else if (req.find("GET /api/guide") != std::string::npos) {
	                if (gFrontendDebugEnable) std::printf("[HTTP API] Route matched: GET /api/guide\n");
	                self->HandleGetTvGuide(clientFd, req); 
	            } 
	            else if (req.find("GET /api/search?q=") != std::string::npos) {
	                if (gFrontendDebugEnable) std::printf("[HTTP API] Route matched: GET /api/search\n");
	                self->HandleApiSearch(clientFd, req);
	            }
	            else if (req.find("GET /api/schedules") != std::string::npos) {
	                if (gFrontendDebugEnable) std::printf("[HTTP ADMIN API] Route matched: GET /api/schedules\n");
	                self->HandleGetSchedules(clientFd);
	            } 
	            else if (req.find("GET /api/tuners") != std::string::npos) { 
	                if (gFrontendDebugEnable) std::printf("[HTTP ADMIN API] Route matched: GET /api/tuners\n");
	                self->HandleGetTuners(clientFd);
	            }
	            else if (req.find("POST /api/schedules/add") != std::string::npos) {
	                if (gFrontendDebugEnable) std::printf("[HTTP ADMIN API] Route matched: POST /api/schedules/add\n");
	                self->HandleAddSchedule(clientFd, req);
	            } 
	            else if (req.find("POST /api/schedules/delete") != std::string::npos) {
	                if (gFrontendDebugEnable) std::printf("[HTTP ADMIN API] Route matched: POST /api/schedules/delete\n");
	                self->HandleDeleteSchedule(clientFd, req);
	            }
	
	            // --- ORIGINAL DLNA MEDIA ENGINE DISPATCHERS (Protected by your runtime flag) ---
	            else if (req.find("GET /description.xml") != std::string::npos) {
	                if (!gFrontendDlnaEnable) {
	                    const char* serviceUnavailable = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
	                    send(clientFd, serviceUnavailable, std::strlen(serviceUnavailable), 0);
	                } else {
	                    if (gFrontendDebugEnable) std::printf("[HTTP THREAD] Route matched: GET description.xml\n");
	                    self->ServeDescription(clientFd);                    
	                }
	            } 
	            else if (req.find("GET /ContentDirectory/scpd.xml") != std::string::npos) {
	                if (gFrontendDebugEnable) std::printf("[HTTP THREAD] Route matched: GET scpd.xml\n");
	                self->ServeScpd(clientFd);   
	            } 
	            else if ((req.find("POST ") == 0 || req.find("\nPOST ") != std::string::npos) && 
	                       (req.find("/ContentDirectory/control") != std::string::npos || 
	                        req.find("/ctl/ContentDir") != std::string::npos)) {
	                if (gFrontendDebugEnable) std::printf("[HTTP THREAD] Route matched safely: POST ContentDirectory\n");
	                self->ServeContentDirectory(clientFd, req);
	            } 
	            else if (req.find("GET /video/") != std::string::npos) {
	                if (gFrontendDebugEnable) std::printf("[HTTP THREAD] Route matched: GET Video Streaming\n");
	                self->StreamVideoFile(clientFd, req);
	            } 
	            
	            // --- UNKNOWN ENDPOINT FALLBACK ---
	            else {
	                const char* notFound = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
	                send(clientFd, notFound, std::strlen(notFound), 0);
	                close(clientFd);
	            }
	
	            // =========================================================================
	            // SIMPLIFIED ROUTE TERMINATION GUARD
	            // =========================================================================
	            // Delegates socket life cycles ONLY to your active long-running library file streams
	            if (req.find("GET /video/") == std::string::npos) {
	                close(clientFd);
	            }
	        });
	        handler.detach();
	    }
       
        if (gFrontendDebugEnable) {
            std::printf("[HTTP DEBUG] Shutting down HTTP loop container.\n");
            std::fflush(stdout);
        }
        close(self->listenFd);
        return B_OK;
    }


    void Start(int serverPort, const std::string& directory, const std::string& ipAddress) {
        // UPDATE: Fail fast if the frontend config toggle has DLNA disabled
        if (!gFrontendDlnaEnable) {
            return;
        }

        port = serverPort;
        rootDir = directory;
        localIp = ipAddress;
        serverThread = spawn_thread(HttpWorkerLoop, "DLNA_HTTP_Engine", B_NORMAL_PRIORITY, this);
        if (serverThread >= 0) resume_thread(serverThread);
    }

private:
    // Endpoint: GET /api/desktop/play?ch=CH — Launches player app natively inside your Haiku desktop environment

    void HandleDesktopAppLaunch(int clientFd, const std::string& requestStr) {
        size_t queryPos = requestStr.find("/api/desktop/play?ch=");
        if (queryPos == std::string::npos) {
            SendJsonResponse(clientFd, 400, "Bad Request", "{\"error\":\"Missing channel parameter\"}");
            return;
        }

        size_t endPos = requestStr.find(" ", queryPos);
        std::string channel = requestStr.substr(queryPos + 21, endPos - (queryPos + 21));

        // 1. Client IP Source Identification via Socket Descriptor
        std::string clientIpStr = "127.0.0.1";
        struct sockaddr_storage peerAddr;
        socklen_t peerAddrLen = sizeof(peerAddr);
        if (getpeername(clientFd, (struct sockaddr*)&peerAddr, &peerAddrLen) == 0) {
            if (peerAddr.ss_family == AF_INET) {
                struct sockaddr_in* s = (struct sockaddr_in*)&peerAddr;
                char ipBuffer[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &(s->sin_addr), ipBuffer, INET_ADDRSTRLEN);
                clientIpStr = ipBuffer;
            }
        }

        // 2. Dynamic Tuner IP Discovery Step (DECLARATION FIRST)
        std::string tunerIp = "";
        std::vector<std::string> discoveredDevices = DiscoverAllTuners();        
        if (!discoveredDevices.empty()) {
            tunerIp = discoveredDevices[0];
        } else {
            tunerIp = "192.168.1.68"; 
        }

        // 3. NOW SAFE TO USE: Reconstruct the direct raw hardware streaming target link 
        std::string targetUrl = "http://" + tunerIp + ":5004/auto/v" + channel;

        // =========================================================================
        // AUTOMATIC SERVER IP DETECTION VIA HTTP HOST HEADERS
        // =========================================================================
        std::string serverIpStr = "";
        size_t hostPos = requestStr.find("Host: ");
        if (hostPos != std::string::npos) {
            size_t valStart = hostPos + 6;
            size_t valEnd = requestStr.find("\r\n", valStart);
            if (valEnd != std::string::npos) {
                std::string fullHost = requestStr.substr(valStart, valEnd - valStart);
                size_t colonIndex = fullHost.find(":");
                serverIpStr = (colonIndex != std::string::npos) ? fullHost.substr(0, colonIndex) : fullHost;
            }
        }

        // HYBRID LOGIC BRAIN: Dynamically evaluate alignment boundaries 
        bool isLocalClient = (clientIpStr == "127.0.0.1" || 
                              clientIpStr == "localhost" || 
                              clientIpStr == serverIpStr);


        if (gFrontendDebugEnable) {
            std::printf("[HYBRID LAUNCHER] Client IP: %s | Detected Server Destination: %s | Match: %s\n", 
                        clientIpStr.c_str(), serverIpStr.c_str(), isLocalClient ? "LOCAL" : "REMOTE");
            std::fflush(stdout);
        }


        if (isLocalClient) {
            // =========================================================================
            // LOCAL MACHINE MODE: Fork and launch player app natively on host screen workspace
            // =========================================================================
            if (gFrontendDebugEnable) {
                std::printf("[HYBRID LAUNCHER] Local environment matched! Initializing system player binaries.\n");
                std::fflush(stdout);
            }

            const char* binaryPath = "/boot/system/bin/mpv";
            if (access("/boot/system/bin/hTV", F_OK) == 0) {
                binaryPath = "/boot/system/bin/hTV";
            } else if (access("/boot/system/bin/vlc", F_OK) == 0) {
                binaryPath = "/boot/system/bin/vlc";
            }

            pid_t processId = fork();
            if (processId == 0) {
                char* playerArgs[3];
                playerArgs[0] = (char*)binaryPath;
                playerArgs[1] = (char*)targetUrl.c_str();
                playerArgs[2] = nullptr; 
                
                execv(playerArgs[0], playerArgs);
                _exit(1); 
            }

            // Send a tiny clean JSON success back to the local browser tab so it can exit its fetch block
            json localResp = {{"status", "success"}, {"mode", "local_launch"}};
            SendJsonResponse(clientFd, 200, "OK", localResp.dump());

        } else {
            // =========================================================================
            // REMOTE NETWORK MODE: Package and forward an interactive .pls streaming container
            // =========================================================================
            if (gFrontendDebugEnable) {
                std::printf("[HYBRID LAUNCHER] Remote user context matched! Streaming PLS shortcut file downstream.\n");
                std::fflush(stdout);
            }

            std::string plsContent = "[playlist]\r\n";
            plsContent += "NumberOfEntries=1\r\n";
            plsContent += "File1=" + targetUrl + "\r\n";
            plsContent += "Title1=HaikuDVR Live - Channel " + channel + "\r\n";
            plsContent += "Length1=-1\r\n";
            plsContent += "Version=2\r\n";

            std::string responseHeaders = 
                "HTTP/1.1 200 OK\r\n"
                "Server: HaikuOS HaikuDVR-MediaServer/1.0\r\n"
                "Content-Type: audio/x-scpls\r\n" 
                "Content-Disposition: attachment; filename=\"live_stream.pls\"\r\n"
                "Content-Length: " + std::to_string(plsContent.length()) + "\r\n"
                "Connection: close\r\n\r\n";

            send(clientFd, responseHeaders.c_str(), responseHeaders.length(), 0);
            send(clientFd, plsContent.c_str(), plsContent.length(), 0);
            close(clientFd);
        }
    }


    // Endpoint: GET / — Serves an in-memory visual web dashboard panel
    void ServeAdminDashboard(int clientFd) {
        std::string html = 
            "<!DOCTYPE html>\n<html>\n<head>\n"
            "<meta name='viewport' content='width=device-width, initial-scale=1.0'>\n"
            "<title>HaikuDVR Web Control Panel</title>\n"
            "<style>\n"
            "  body { font-family: -apple-system,system-ui,BlinkMacSystemFont,\"Segoe UI\",Roboto,sans-serif; background: #121212; color: #e0e0e0; margin: 0; padding: 20px; }\n"
            "  .container { max-width: 1100px; margin: 0 auto; }\n"
            "  h1, h2 { color: #ffffff; border-bottom: 1px solid #2d2d2d; padding-bottom: 10px; }\n"
            "  .grid { display: grid; grid-template-columns: 1fr; gap: 20px; }\n"
            "  @media(min-width: 768px) { .grid { grid-template-columns: 2fr 1fr; } }\n"
            "  .card { background: #1e1e1e; border: 1px solid #2d2d2d; border-radius: 8px; padding: 15px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); }\n"
            "  .program-row { border-bottom: 1px solid #2d2d2d; padding: 12px 0; display: flex; justify-content: space-between; align-items: center; cursor: pointer; }\n"
            "  .program-row:hover { background: #252525; }\n"
            "  .badge { background: #3a3a3a; color: #fff; padding: 2px 8px; border-radius: 4px; font-size: 0.85em; font-weight: bold; }\n"
            "  .btn { background: #0078d7; color: white; border: none; padding: 6px 12px; border-radius: 4px; cursor: pointer; font-size: 0.9em; }\n"
            "  .btn-del { background: #a80000; }\n"
            "  .form-group { margin-bottom: 12px; }\n"
            "  label { display: block; margin-bottom: 5px; font-size: 0.9em; color: #aaa; }\n"
            "  input, select { width: 100%; padding: 8px; background: #2d2d2d; border: 1px solid #3a3a3a; color: #fff; border-radius: 4px; box-sizing: border-box; }\n"
            "  /* MINIMAL TARGETED MODAL STYLING OVERLAYS */\n"
            "  .modal { display: none; position: fixed; z-index: 1000; left: 0; top: 0; width: 100%; height: 100%; background: rgba(0,0,0,0.85); align-items: center; justify-content: center; }\n"
            "  .modal-content { background: #1e1e1e; border: 1px solid #3a3a3a; padding: 20px; border-radius: 8px; max-width: 600px; width: 95%; position: relative; }\n"
            "  .close-btn { position: absolute; right: 15px; top: 10px; color: #aaa; font-size: 24px; cursor: pointer; }\n"
            "  video { width: 100%; border-radius: 4px; margin-top: 15px; background: #000; box-shadow: 0 4px 10px rgba(0,0,0,0.5); }\n"
            "  /* RED PULSATING RECORDING BADGE STYLES */\n"
            "  .badge-rec { background: #a80000; color: #fff; padding: 2px 8px; border-radius: 4px; font-size: 0.8em; font-weight: bold; animation: pulseRed 1.8s infinite ease-in-out; margin-left: 8px; display: inline-flex; align-items: center; gap: 4px; }\n"
            "  @keyframes pulseRed { 0% { opacity: 0.4; } 50% { opacity: 1; } 100% { opacity: 0.4; } }\n"
            "</style>\n"
            "</head>\n<body>\n"
            "<div class='container'>\n"
            "  <h1>HaikuDVR Web Control Panel</h1>\n"
            "  <div class='grid'>\n"
            "    <div class='card'>\n"
			"      <div style='display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; border-bottom:1px solid #2d2d2d; margin-bottom:10px; padding-bottom:5px;'>\n"
			"        <h2 style='border-bottom:none; margin:0;'>TV Program Guide</h2>\n"
			"        <div style='display:flex; gap:8px; align-items:center;'>\n"
			"          <input type='date' id='guide-date' style='width:130px; padding:4px;'>\n"
			"          <input type='time' id='guide-time' style='width:90px; padding:4px;'>\n"
			"          <button class='btn' onclick='loadGuide()' style='padding:5px 10px;'>Go</button>\n"
			"        </div>\n"
			"        <input type='text' id='search-box' placeholder='Search program titles...' style='width:200px;'>\n"
			"      </div>\n"
            "      <div id='guide-target'>Loading guide rows from engine...</div>\n"
            "    </div>\n"
            "    <div>\n"

			"      <div class='card' style='margin-bottom: 20px;'>\n"
			"        <h2>Quick-Schedule Recording</h2>\n"
			"        <form id='sched-form'>\n"
			"          <!-- HIDDEN FIELD FOR SHOW DESCRIPTIONS -->\n"
			"          <input type='hidden' id='hidden-desc' value=''>\n"
			"          <div class='form-group'><label>Show Title</label><input type='text' id='title' required value='Web Scheduled Recording'></div>\n"
			"          <div class='form-group'><label>Channel (LCN)</label><input type='text' id='channel' placeholder='e.g. 5.1' required></div>\n"

			"          <div class='form-group'><label>Start Date</label><input type='date' id='date' required></div>\n"
			"          <div class='form-group'><label>Start Time</label><input type='time' id='time' required></div>\n"
			"          <div class='form-group'><label>Target Tuner Hardware (HDHomeRun)</label>\n"
			"            <select id='tuner-ip'>\n"
			"              <option value=''>Auto-Select (Default)</option>\n"
			"              <!-- Populated dynamically via the libhdhomerun endpoint -->\n"
			"            </select>\n"
			"          </div>\n"
			"          <div class='form-group'><label>Duration</label>"
			"            <select id='duration'>\n"
			"              <option value='1800'>30 Minutes</option>\n"
			"              <option value='3600' selected>1 Hour</option>\n"
			"              <option value='7200'>2 Hours</option>\n"
			"              <option value='10800'>3 Hours</option>\n" 
			"              <option value='14400'>4 Hours</option>\n" 
			"            </select>\n"
			"          </div>\n"
			"          <button type='submit' class='btn'>Add Active Recording Task</button>\n"
			"        </form>\n"
			"      </div>\n"

            "      <div class='card' style='margin-bottom: 20px;'>\n"
            "        <h2>Active Task Schedules</h2>\n"
            "        <div id='schedule-target'>No active tasks recorded.</div>\n"
            "      </div>\n"

            "      <!-- NEW: RECORDINGS LIBRARY PANEL CARD -->\n"
            "      <div class='card'>\n"
            "        <h2>Completed Recordings Library</h2>\n"
            "        <div id='recordings-target'>Scanning disk for media assets...</div>\n"
            "      </div>\n"

            "    </div>\n"
            "  </div>\n" // Ends master grid panel container layout block safely

            "</div>\n"
            "\n"
            "<!-- NATIVE INFRASTRUCTURE FOR PROGRAM VISUAL MODALS -->\n"
            "<div id='details-modal' class='modal'>\n"
            "  <div class='modal-content'>\n"
            "    <span class='close-btn' onclick='closeModal()'>&times;</span>\n"
            "    <h2 id='modal-title' style='margin-top:0;'>Show Title</h2>\n"
            "    <div style='margin: 10px 0;'><span id='modal-channel' class='badge'>Ch --</span> <span id='modal-time' style='color:#aaa; font-size:0.9em;'>00:00</span></div>\n"
            "    <p id='modal-desc' style='color:#ccc; font-size:0.95em; line-height:1.4;'>Loading catalog record attributes...</p>\n"
            "    <div id='player-container'></div>\n"
            "    <div style='margin-top:15px; text-align:right;'><button id='modal-action-btn' class='btn'>Record Program</button></div>\n"
            "  </div>\n"
            "</div>\n"
            "\n"
            "<script>\n"
            "  let guideCache = [];\n"
            "  let scheduleCache = [];\n"
            "  let activeLivePlayer = null;\n"
            "  let currentSelectedDescription = '';\n" 
            "\n"
            "  function loadGuide() {\n"
            "    let dt = document.getElementById('guide-date').value;\n"
            "    let tm = document.getElementById('guide-time').value;\n"
            "    let url = '/api/guide';\n"
            "    if(dt && tm) { url += '?dt=' + dt + '&tm=' + encodeURIComponent(tm); }\n"
            "    \n"
            "    fetch(url).then(r => r.json()).then(data => {\n"
            "      guideCache = data;\n"
            "      let html = '';\n"
            "      if(data.length === 0) { html = '<p style=\"color:#888; padding:10px;\">No broadcasts found for this time slot.</p>'; }\n"
            "      data.forEach((p, idx) => {\n"
            "        let displayDate = p.air_date ? p.air_date + ' | ' : '';\n"
            "        html += '<div class=\"program-row\" onclick=\"showProgramDetails(' + idx + ', false)\"><div><strong>' + p.title + '</strong><br><small style=\"color:#888;\">' + displayDate + p.start_time + ' - ' + p.end_time + '</small></div><div style=\"text-align:right;\"><span class=\"badge\">Ch ' + p.channel + '</span></div></div>';\n"
            "      });\n"
            "      document.getElementById('guide-target').innerHTML = html;\n"
            "    });\n"
            "  }\n"
            "\n"
			"  function loadGuide() {\n"
			"    let dt = document.getElementById('guide-date').value;\n"
			"    let tm = document.getElementById('guide-time').value;\n"
			"    let url = '/api/guide';\n"
			"    if(dt && tm) { url += '?dt=' + dt + '&tm=' + encodeURIComponent(tm); }\n"
			"    \n"
			"    fetch(url).then(r => r.json()).then(data => {\n"
			"      guideCache = data;\n"
			"      let html = '';\n"
			"      if(data.length === 0) { html = '<p style=\"color:#888; padding:10px;\">No broadcasts found for this time slot.</p>'; }\n"
			"      data.forEach((p, idx) => {\n"
			"        let displayDate = p.air_date ? p.air_date + ' | ' : '';\n"
			"        html += '<div class=\"program-row\" onclick=\"showProgramDetails(' + idx + ', false)\"><div><strong>' + p.title + '</strong><br><small style=\"color:#888;\">' + displayDate + p.start_time + ' - ' + p.end_time + '</small></div><div style=\"text-align:right;\"><span class=\"badge\">Ch ' + p.channel + '</span></div></div>';\n"
			"      });\n"
			"      document.getElementById('guide-target').innerHTML = html;\n"
			"    });\n"
			"  }\n"
			"  function loadSchedules() {\n"
			"    fetch('/api/schedules').then(r => r.json()).then(data => {\n"
			"      scheduleCache = data;\n"
			"      let target = document.getElementById('schedule-target');\n"
			"      target.innerHTML = '';\n"
			"      \n"
			"      if(data.length === 0) {\n"
			"        target.innerHTML = '<p style=\"color:#888;\">No upcoming recording schedules.</p>';\n"
			"        return;\n"
			"      }\n"
			"      \n"
			"      data.forEach((s, idx) => {\n"
			"        let cleanTitle = s.show_title.replace(/_/g, ' ');\n"
			"        \n"
			"        let row = document.createElement('div');\n"
			"        row.className = 'program-row';\n"
			"        row.style.display = 'flex';\n"
			"        row.style.justifyContent = 'space-between';\n"
			"        row.style.alignItems = 'center';\n"
			"        row.onclick = () => showProgramDetails(idx, true);\n"
			"        \n"
			"        row.innerHTML = '<div><strong>' + cleanTitle + '</strong><br><small style=\"color:#aaa;\">Ch ' + s.channel + ' | ' + s.date + '</small></div>' +\n"
			"                        '<div><button class=\"btn btn-del\">Drop</button></div>';\n"
			"        \n"
			"        let btn = row.querySelector('.btn-del');\n"
			"        btn.onclick = (e) => {\n"
			"          e.stopPropagation();\n"
			"          deleteSched(s.date, s.time, s.channel);\n"
			"        };\n"
			"        \n"
			"        target.appendChild(row);\n"
			"        \n"
			"      });\n"
			"    });\n"
			"  }\n"
            "\n"
			"  function showProgramDetails(index, isSchedule) {\n"
			"    let item = isSchedule ? scheduleCache[index] : guideCache[index];\n"
			"    let title = isSchedule ? item.show_title.replace(/_/g, ' ') : item.title;\n"
			"    \n"
			"    document.getElementById('modal-title').innerText = title;\n"
			"    document.getElementById('modal-channel').innerText = 'Ch ' + item.channel;\n"
			"    document.getElementById('modal-time').innerText = isSchedule ? (item.date + ' @ ' + item.time) : (item.start_time + ' - ' + item.end_time);\n"
			"    document.getElementById('modal-desc').innerText = item.description || 'No additional catalog info recorded for this station broadcast event.';\n"
			"    \n"
			"    let playerBox = document.getElementById('player-container'); playerBox.innerHTML = '';\n"
			"    let actionBtn = document.getElementById('modal-action-btn');\n"
			"    \n"
			"    if(isSchedule) {\n"
			"      actionBtn.style.display = 'none';\n"
			"    } else {\n"
			"      actionBtn.style.display = 'inline-block'; actionBtn.innerText = 'Schedule Recording';\n"
			"      actionBtn.onclick = () => {\n"
			"        document.getElementById('title').value = title;\n"
			"        document.getElementById('channel').value = item.channel;\n"
			"        \n"
			"        // FIXED: Push description info cleanly into the form storage right here\n"
			"        document.getElementById('hidden-desc').value = item.description || item.show_description || 'No description available.';\n"
			"        \n"
			"        if (item.air_date) {\n"
			"          document.getElementById('date').value = item.air_date;\n"
			"        }\n"
			"        if (item.start_time) {\n"
			"          document.getElementById('time').value = item.start_time;\n"
			"        }\n"
			"        \n"
			"        // FIXED: Dynamically map the closest program runtime duration option\n"
			"        if (item.duration) {\n"
			"          let durationSelect = document.getElementById('duration');\n"
			"          \n"
			"          // Default to 1 hour (3600s) if no exact match is discovered\n"
			"          durationSelect.value = '3600'; \n"
			"          \n"
			"          // Cycle through choices to see if show runtime matches an option\n"
			"          for (let option of durationSelect.options) {\n"
			"            if (Math.abs(parseInt(option.value) - item.duration) < 300) { // 5-minute safety threshold window\n"
			"              durationSelect.value = option.value;\n"
			"              break;\n"
			"            }\n"
			"          }\n"
			"        }\n"
			"        \n"
			"        closeModal(); \n"
			"        document.getElementById('title').focus();\n"
			"      };\n"

			"      \n"
			"      // CLIENT-SIDE ADAPTIVE HYBRID FETCH ROUTE\n"
			"      playerBox.innerHTML = '<button class=\"btn\" style=\"background:#107c41; margin-bottom:10px; width:100%; padding:10px;\" id=\"play-live-btn\">Watch Live</button>';\n"
			"      setTimeout(() => {\n"
			"        let btn = document.getElementById('play-live-btn');\n"
			"        if(btn) btn.onclick = () => {\n"
			"          closeModal();\n"
			"          \n"
			"          // FIXED: We handle everything silently via fetch. No window.location.href redirects at the top!\n"
			"          fetch('/api/desktop/play?ch=' + item.channel)\n"
			"            .then(res => {\n"
			"              let contentType = res.headers.get('content-type');\n"
			"              if (contentType && contentType.includes('audio/x-scpls')) {\n"
			"                // REMOTE CLIENT: Transform payload into an active playlist file trigger\n"
			"                return res.blob().then(blob => {\n"
			"                  let url = window.URL.createObjectURL(blob);\n"
			"                  let a = document.createElement('a');\n"
			"                  a.href = url; a.download = 'live_stream.pls';\n"
			"                  document.body.appendChild(a); a.click(); a.remove();\n"
			"                });\n"
			"              } else {\n"
			"                // LOCAL CLIENT: Local fork handled on backend, do nothing on the browser side\n"
			"                return res.json();\n"
			"              }\n"
			"            }).catch(err => console.log('Playback handshake failed', err));\n"
			"        };\n"
			"      }, 50);\n"
			"    }\n"
			
			"    document.getElementById('details-modal').style.display = 'flex';\n"
			"  }\n"
            "  \n"
			"  function destroyActivePlayer() {\n"
			"      if(activeLivePlayer !== null) {\n"
			"          try {\n"
			"              activeLivePlayer.pause(); activeLivePlayer.unload();\n"
			"              activeLivePlayer.detachMediaElement(); activeLivePlayer.destroy();\n"
			"          } catch(e) {}\n"
			"          activeLivePlayer = null;\n"
			"      }\n"
			"  }\n"
			"  \n"
			"  function closeModal() { destroyActivePlayer(); document.getElementById('details-modal').style.display = 'none'; }\n"
			"  \n"
			"  // FIX: Added 'application/json' content-type headers so the C++ engine can read the payload body\n"
			"  function deleteSched(date, time, channel) {\n"
			"    if(confirm('Drop this scheduled task?')) {\n"
			"      fetch('/api/schedules/delete', {\n"
			"        method: 'POST',\n"
			"        headers: { 'Content-Type': 'application/json' },\n"
			"        body: JSON.stringify({ date, time, channel })\n"
			"      }).then(() => { loadSchedules(); });\n"
			"    }\n"
			"  }\n"
			"  \n"
			
			"  document.getElementById('sched-form').addEventListener('submit', (e) => {\n"
			"    e.preventDefault();\n"
			"    let payload = {\n"
			"      title: document.getElementById('title').value,\n"
			"      channel: document.getElementById('channel').value,\n"
			"      date: document.getElementById('date').value,\n"
			"      time: document.getElementById('time').value,\n"
			"      tuner_ip: document.getElementById('tuner-ip').value,\n"
			"      duration: document.getElementById('duration').value,\n"
			"      description: document.getElementById('hidden-desc').value\n" 
			"    };\n"
			"    \n"
			"    fetch('/api/schedules/add', {\n"
			"      method: 'POST',\n"
			"      headers: { 'Content-Type': 'application/json' },\n"
			"      body: JSON.stringify(payload)\n"
			"    }).then(r => {\n"
			"      if(r.ok) {\n"
			"        loadSchedules();\n"
			"        document.getElementById('title').value = '';\n"
			"        document.getElementById('channel').value = '';\n"
			"        document.getElementById('tuner-ip').value = '';\n"
			"        document.getElementById('hidden-desc').value = '';\n" 
			"      } else {\n"
			"        alert('Failed to save schedule on server.');\n"
			"      }\n"
			"    });\n"
			"  });\n"
			
			"  \n"

            "  document.getElementById('search-box').addEventListener('input', (e) => {\n"
            "      let query = e.target.value.trim();\n"
            "      if (query.length < 2) {\n"
            "        loadGuide();\n"
            "        return;\n"
            "      }\n"
            "      fetch('/api/search?q=' + encodeURIComponent(query))\n"
            "        .then(r => r.json())\n"
            "        .then(data => {\n"
            "          guideCache = data;\n"
            "          let html = '';\n"
            "          if (data.length === 0) { html = '<p style=\"color:#888; padding:10px;\">No matching programs discovered.</p>'; }\n"
            "          data.forEach((p, idx) => {\n"
            "            let displayDate = p.air_date ? p.air_date + ' | ' : '';\n"
            "            html += `<div class=\"program-row\" onclick=\"showProgramDetails(${idx}, false)\"><div><strong>${p.title}</strong><br><small style=\"color:#888;\">${displayDate}${p.start_time} - ${p.end_time}</small></div><div style=\"text-align:right;\"><span class=\"badge\">Ch ${p.channel}</span></div></div>`;\n"
            "          });\n"
            "          document.getElementById('guide-target').innerHTML = html;\n"
            "        });\n"
            "  });\n"

			"  \n"
			"  try {\n"
			"    let now = new Date();\n"
			"    // Sweedish locale safely yields standard YYYY-MM-DD format output\n"
			"    let localISODate = now.toLocaleDateString('sv');\n"
			"    \n"
			"    // Safely extract hours and minutes\n"
			"    let hh = String(now.getHours()).padStart(2, '0');\n"
			"    let mm = String(now.getMinutes()).padStart(2, '0');\n"
			"    let localISOTime = hh + ':' + mm;\n"
			"    \n"
			"    document.getElementById('date').value = localISODate;\n"
			"    document.getElementById('guide-date').value = localISODate;\n"
			"    document.getElementById('guide-time').value = localISOTime;\n"
			"  } catch(err) {}\n"
			"  \n"
			  // Tuner IP Function
			"	  function loadTuners() {\n"
			"	    fetch('/api/tuners')\n"
			"	      .then(r => r.json())\n"
			"	      .then(tunerList => {\n"
			"	        let select = document.getElementById('tuner-ip');\n"
				        // Clear previous state and reset the fallback choice anchor
			"	        select.innerHTML = '<option value="">Auto-Select (Default)</option>';\n"
				        
			"	        if (!tunerList || tunerList.length === 0) {\n"
			"	          let opt = document.createElement('option');\n"
			"	          opt.value = '';\n"
			"	          opt.innerText = 'No HDHomeRun hardware detected';\n"
			"	          opt.disabled = true;\n"
			"	          select.appendChild(opt);\n"
			"	          return;\n"
			"	        }\n"
				
			"	        tunerList.forEach(ip => {\n"
			"	          let opt = document.createElement('option');\n"
			"	          opt.value = ip;\n"
			"	          opt.innerText = 'HDHomeRun Unit (' + ip + ')';\n"
			"	          select.appendChild(opt);\n"
			"	        });\n"
			"	      })\n"
			"	      .catch(err => console.error('Could not communicate with HDHomeRun tuner API', err));\n"
			"	  }\n"
				
				  // Find boot sequence line and register it:
			"	  try {\n"
			"	    let now = new Date();\n"
			"	    let localISODate = now.toLocaleDateString('sv');\n"
			"	    let hh = String(now.getHours()).padStart(2, '0');\n"
			"	    let mm = String(now.getMinutes()).padStart(2, '0');\n"
			"	    let localISOTime = hh + ':' + mm;\n"
				    
			"	    document.getElementById('date').value = localISODate;\n"
			"	    document.getElementById('guide-date').value = localISODate;\n"
			"	    document.getElementById('guide-time').value = localISOTime;\n"
			"	  } catch(err) {}\n"				  

			  // RUN HARDWARE DISCOVERY ON PAGE LOAD
			"  loadGuide();\n"
			"  loadSchedules();\n" 
			"  loadTuners();\n" 
			"  loadRecordings();\n"
			"  setInterval(loadSchedules, 6000);\n"
			"  setInterval(loadRecordings, 6000);\n" 

			  // Unified deleteSched hook handles task teardowns, cancellations, and binary drop cleanups natively
			"  function deleteSched(date, time, channel) {\n"
			"    if(confirm('Are you sure you want to drop this recording schedule and permanently delete any associated video files from storage?')) {\n"
			"      fetch('/api/schedules/delete', {\n"
			"        method: 'POST',\n"
			"        headers: { 'Content-Type': 'application/json' },\n"
			"        body: JSON.stringify({ date, time, channel })\n"
			"      })\n"
			"      .then(res => res.json())\n"
			"      .then(data => {\n"
			"        if (data.status === 'success') {\n"
			"          loadSchedules();\n"
			"          loadRecordings();\n"
			"          if (data.file_deleted) {\n"
			"             console.log('Physical recording file cleaned up successfully from Haiku media storage.');\n"
			"          }\n"
			"        } else {\n"
			"          alert('Error from server: ' + (data.message || 'Failed to complete deletion task.'));\n"
			"        }\n"
			"      })\n"
			"      .catch(err => {\n"
			"         console.error('Network failure connecting to HaikuDVR deletion endpoint:', err);\n"
			"         alert('Network error encountered while deleting record.');\n"
			"      });\n"
			"    }\n"
			"  }\n"

			  // --- LIVE HYBRID STREAMING PLAYBACK METHOD CONTROLLERS ---
			"  function loadRecordings() {\n"
			"    fetch('/api/recordings')\n"
			"      .then(r => r.json())\n"
			"      .then(data => {\n"
			"        let target = document.getElementById('recordings-target');\n"
			"        target.innerHTML = '';\n"
			"        if(!data || data.length === 0) {\n"
			"          target.innerHTML = '<p style=\"color:#888; padding:5px;\">No recorded .ts video streams found on hard disk.</p>';\n"
			"          return;\n"
			"        }\n"
			"        data.forEach(rec => {\n"
			"          let row = document.createElement('div');\n"
			"          row.className = 'program-row';\n"
			"          row.style.display = 'flex';\n"
			"          row.style.justifyContent = 'space-between';\n"
			"          row.style.alignItems = 'center';\n"
			"          row.style.padding = '10px 0';\n"
			
			"          let recBadge = rec.is_recording ? '<span class=\"badge-rec\">&bull; REC</span>' : '';\n"
			"          let deleteBtnLabel = rec.is_recording ? 'Abort' : 'Delete File';\n"
			
			"          row.innerHTML = '<div>' +\n"
			"                          '<strong>' + rec.title + recBadge + '</strong><br>' +\n"
			"                          '<small style=\"color:#aaa;\">Ch ' + rec.channel + ' | ' + rec.date + ' &bull; <span style=\"color:#0078d7;\">' + rec.size_mb + ' MB</span></small>' +\n"
			"                          '</div>' +\n"
			"                          '<div style=\"display:flex; gap:6px;\">' +\n" // Button container flex spacer
			"                            '<button class=\"btn btn-play\" style=\"background:#107c41; padding: 4px 8px; font-size: 0.85em;\">Play</button>' +\n"
			"                            '<button class=\"btn btn-del\" style=\"padding: 4px 8px; font-size: 0.85em;\">' + deleteBtnLabel + '</button>' +\n"
			"                          '</div>';\n"
			
			              // Play Button Handling 
			"          let playBtn = row.querySelector('.btn-play');\n"
			"          playBtn.onclick = (e) => {\n"
			"            e.stopPropagation();\n"
			"            fetch('/api/recordings/play?file=' + encodeURIComponent(rec.filename))\n"
			"              .then(res => {\n"
			"                let contentType = res.headers.get('content-type');\n"
			"                if (contentType && contentType.includes('audio/x-scpls')) {\n"
			"                  return res.blob().then(blob => {\n"
			"                    let url = window.URL.createObjectURL(blob);\n"
			"                    let a = document.createElement('a');\n"
			"                    a.href = url; a.download = rec.filename + '.pls';\n"
			"                    document.body.appendChild(a); a.click(); a.remove();\n"
			"                  });\n"
			"                }\n"
			"              }).catch(err => console.error('Library media handshake verification failed', err));\n"
			"          };\n"

			              // Delete Button Handling
			"          let delBtn = row.querySelector('.btn-del');\n"
			"          delBtn.onclick = (e) => {\n"
			"            e.stopPropagation();\n"
			"            deletePhysicalFileRecord(rec.date, rec.channel, rec.title);\n"
			"          };\n"
			
			"          target.appendChild(row);\n"
			"        });\n"
			"      })\n"
			"      .catch(err => {\n"
			"         document.getElementById('recordings-target').innerHTML = '<p style=\"color:#a80000;\">Failed to fetch library asset updates.</p>';\n"
			"      });\n"
			"  }\n"

			  // --- LINK BACKEND DELETION FOR DISK ASSETS ---
			"  function deletePhysicalFileRecord(date, channel, rawTitle) {\n"
			"    if(confirm('Permanently delete this physical video file recording from your Haiku system workspace? This action cannot be reversed.')) {\n"
			"      let formattedTitle = rawTitle.replace(/ /g, '_');\n"
			"      fetch('/api/schedules/delete', {\n"
			"        method: 'POST',\n"
			"        headers: { 'Content-Type': 'application/json' },\n"
			"        body: JSON.stringify({ date: date, time: \"00:00\", channel: channel, title_override: formattedTitle })\n"
			"      })\n"
			"      .then(res => res.json())\n"
			"      .then(data => {\n"
			"         loadSchedules();\n"
			"         loadRecordings();\n"
			"      });\n"
			"    }\n"
			"  }\n"

			"</script>\n"
			"\n"
			"</body>\n</html>\n";



        std::string response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " + std::to_string(html.length()) + "\r\nConnection: close\r\n\r\n" + html;
        send(clientFd, response.c_str(), response.length(), 0);
    }


    std::string ExtractHttpRequestBody(const std::string& requestStr) {
        size_t bodyPos = requestStr.find("\r\n\r\n");
        if (bodyPos != std::string::npos) return requestStr.substr(bodyPos + 4);
        return "";
    }



	// Standardized helper utility to dispatch clean RESTful JSON responses
	void SendJsonResponse(int clientFd, int statusCode, const std::string& statusMessage, const std::string& jsonContent) {
	    std::string response = 
	        "HTTP/1.1 " + std::to_string(statusCode) + " " + statusMessage + "\r\n"
	        "Content-Type: application/json; charset=utf-8\r\n"
	        "Content-Length: " + std::to_string(jsonContent.length()) + "\r\n"
	        "Access-Control-Allow-Origin: *\r\n" // Allows easy third-party web panel interfaces
	        "Connection: close\r\n\r\n" + jsonContent;
	    send(clientFd, response.c_str(), response.length(), 0);
	}
	
	// Security Helper to avoid path traversal out of bounds (e.g. /../boot/system)
	bool IsPathSafeAndContained(const std::string& targetPath, const std::string& baseDir) {
	    char resolvedTarget[PATH_MAX];
	    char resolvedBase[PATH_MAX];
	    
	    if (realpath(baseDir.c_str(), resolvedBase) == nullptr) return false;
	    
	    // If file doesn't exist yet, check its containing directory path boundary instead
	    if (realpath(targetPath.c_str(), resolvedTarget) == nullptr) {
	        size_t lastSlash = targetPath.find_last_of("/");
	        if (lastSlash == std::string::npos) return false;
	        std::string dirPart = targetPath.substr(0, lastSlash);
	        if (realpath(dirPart.c_str(), resolvedTarget) == nullptr) return false;
	    }
	    
	    std::string strTarget(resolvedTarget);
	    std::string strBase(resolvedBase);
	    return (strTarget.rfind(strBase, 0) == 0);
	}
	
	void HandleDeleteSchedule(int clientFd, const std::string& requestStr) {
	    std::string body = ExtractHttpRequestBody(requestStr);
	    
	    // DEBUG STEP 1: Verify incoming raw network payload string data
	    std::printf("\n================ [DEBUG DELETION] ================\n");
	    std::printf("[DEBUG] Incoming Request Body: %s\n", body.empty() ? "EMPTY" : body.c_str());
	    std::fflush(stdout);
	
	    if (body.empty()) {
	        SendJsonResponse(clientFd, 400, "Bad Request", "{\"status\":\"error\",\"message\":\"Empty JSON payload\"}");
	        return;
	    }
	
	    try {
	        auto jIn = json::parse(body);
	        
	        // Check for required elements
	        if (!jIn.contains("date") || !jIn.contains("channel")) {
	            std::printf("[DEBUG ERROR] Missing basic JSON fields (date or channel).\n");
	            std::fflush(stdout);
	            SendJsonResponse(clientFd, 400, "Bad Request", "{\"status\":\"error\",\"message\":\"Missing required fields\"}");
	            return;
	        }
	
	        std::string date = jIn["date"];
	        std::string channel = jIn["channel"];
	        std::string time = jIn.value("time", "00:00");
	        
	        // Handle optional frontend fields gracefully
	        std::string titleOverride = jIn.value("title_override", "");
	
	        std::printf("[DEBUG] Parsed Fields -> Date: %s | Time: %s | Channel: %s | Title Override: %s\n", 
	                    date.c_str(), time.c_str(), channel.c_str(), titleOverride.empty() ? "NONE" : titleOverride.c_str());
	        std::fflush(stdout);
	
	        bool foundInSchedule = false;
	        std::string targetFilePath = "";
	        
	        // Lock and attempt to match an active schedule item first
	        gScheduleLocker.Lock();
	        for (auto it = gScheduleList.begin(); it != gScheduleList.end(); ++it) {
	            if (it->startDate == date && it->startTime == time && it->channel == channel) {
	                foundInSchedule = true;
	                
	                std::string cleanTitle = it->showTitle;
	                std::replace(cleanTitle.begin(), cleanTitle.end(), ' ', '_');
	                targetFilePath = gGlobalSaveDirectory + "/" + cleanTitle + "_" + it->startDate + "_" + it->channel + ".ts";
	                
	                std::printf("[DEBUG] Found item match inside gScheduleList array!\n");
	                std::printf("[DEBUG] Generated target file path from schedule metadata: %s\n", targetFilePath.c_str());
	                std::fflush(stdout);
	
	                // Check and kill active curl streaming threads
	                gRunningWorkersLocker.Lock();
	                auto workerIt = gRunningWorkersMap.find(targetFilePath);
	                if (workerIt != gRunningWorkersMap.end()) {
	                    std::printf("[DEBUG ALERT] This file is currently recording! Triggering cancellation flag context.\n");
	                    if (workerIt->second.cancellationFlag != nullptr) {
	                        atomic_set(workerIt->second.cancellationFlag, 1);
	                    }
	                }
	                gRunningWorkersLocker.Unlock();
	
	                gScheduleList.erase(it);
	                break;
	            }
	        }
	        gScheduleLocker.Unlock();
	
	        // FALLBACK: If it wasn't found in memory schedules list, it's an old file or finished recording.
	        // We use our clean title override from the library panel array directly!
	        if (!foundInSchedule) {
	            std::printf("[DEBUG] Item not found inside active schedule RAM lists. Dropping down to library fallback lookup style.\n");
	            if (!titleOverride.empty()) {
	                targetFilePath = gGlobalSaveDirectory + "/" + titleOverride + "_" + date + "_" + channel + ".ts";
	                std::printf("[DEBUG] Generated fallback library file target path: %s\n", targetFilePath.c_str());
	            } else {
	                std::printf("[DEBUG WARNING] Deletion target missing from schedule and no title_override was supplied by frontend.\n");
	            }
	            std::fflush(stdout);
	        }
	
	        // Save modification changes down onto disk state
	        SaveSchedulesToDisk();
	
	        // FILE DISK ERASURE LAYER
	        bool physicalFileDeleted = false;
	        if (!targetFilePath.empty()) {
	            std::printf("[DEBUG] Testing filesystem access checks for path: %s\n", targetFilePath.c_str());
	            std::fflush(stdout);
	
	            if (access(targetFilePath.c_str(), F_OK) == 0) {
	                std::printf("[DEBUG] File exists on hard drive filesystem! Running path safety container check...\n");
	                std::fflush(stdout);
	
	                if (IsPathSafeAndContained(targetFilePath, gGlobalSaveDirectory)) {
	                    std::printf("[DEBUG] Safety boundary pass! Running POSIX unlink() on device file now.\n");
	                    std::fflush(stdout);
	
	                    if (unlink(targetFilePath.c_str()) == 0) {
	                        physicalFileDeleted = true;
	                        std::printf("[DEBUG SUCCESS] Physical file completely deleted from system hard disk!\n");
	                    } else {
	                        std::printf("[DEBUG ERROR] POSIX unlink system call failed with error text: %s\n", strerror(errno));
	                    }
	                } else {
	                    std::printf("[DEBUG SECURITY FAIL] Path safety evaluation rejected the target path modification!\n");
	                }
	            } else {
	                std::printf("[DEBUG ERROR] File check skipped. access(F_OK) reported that file does not exist or is inaccessible.\n");
	            }
	            std::fflush(stdout);
	        }
	
	        std::printf("==================================================\n\n");
	        std::fflush(stdout);
	
	        json jOut = {
	            {"status", "success"},
	            {"message", foundInSchedule ? "Schedule dropped" : "Completed item processed"},
	            {"file_deleted", physicalFileDeleted}
	        };
	        SendJsonResponse(clientFd, 200, "OK", jOut.dump());
	
	    } catch (const std::exception& e) {
	        std::printf("[DEBUG EXCEPTION CRASH] JSON processing caught error logic: %s\n", e.what());
	        std::printf("==================================================\n\n");
	        std::fflush(stdout);
	        
	        json err = {{"status", "error"}, {"message", e.what()}};
	        SendJsonResponse(clientFd, 500, "Internal Server Error", err.dump());
	    }
	}


	bool VerifyAndRefreshGuideCache() {
	    const std::string localPath = "/boot/home/config/settings/HaikuDVR/guide.db";
	    // Synchronized perfectly with your frontend's layout timestamp tracker file
	    const std::string cacheControlPath = "/boot/home/config/settings/HaikuDVR/guide_master.xml.cache";
	    
	    if (gFrontendDebugEnable) std::printf("[DVR DEBUG] Checking backend time-window cache status...\n");
	
	    struct stat attrib;
	    // If the master database file itself is completely missing, force a refresh/sync instantly
	    if (stat(localPath.c_str(), &attrib) != 0) {
	        if (gFrontendDebugEnable) std::printf("[DVR DEBUG] Local master guide database missing. Forcing sync trigger.\n");
	        return true; 
	    }
	
	    uint32 savedSyncTime = 0;
	    std::ifstream cacheIn(cacheControlPath);
	    if (cacheIn.is_open()) {
	        cacheIn >> savedSyncTime;
	        cacheIn.close();
	    }
	
	    // Set a 3-day expiration window for the master payload database (259200 seconds)
	    uint32 cacheExpirationWindow = 259200; 
	    uint32 currentTime = static_cast<uint32>(std::time(nullptr));
	
	    if (savedSyncTime > 0 && (currentTime - savedSyncTime) < cacheExpirationWindow) {
	        if (gFrontendDebugEnable) {
	            uint32 remainingTime = cacheExpirationWindow - (currentTime - savedSyncTime);
	            std::printf("[DVR DEBUG] SUCCESS: Backend guide cache is %u secs old (Expires in %u mins). Skipping remote updates.\n", 
	                        (currentTime - savedSyncTime), remainingTime / 60);
	        }
	        return false; // Cache is perfectly fine, don't execute network overhead or downloads
	    }
	
	    if (gFrontendDebugEnable) std::printf("[DVR DEBUG] Cache window expired or invalid for backend payload. Initializing updates.\n");
	
	    // =========================================================================
	    //  AUTOMATED CACHE PURGE ENGINE (REMOVES EXPIRED DAILY CHUNK FILES)
	    // =========================================================================
	    std::printf("[DVR PURGE] Running backend storage sweep routine on expired guide chunks...\n");
	    
	    std::time_t rawToday = std::time(nullptr);
	    std::tm* localToday = std::localtime(&rawToday);
	    char todayBuf[16];
	    std::strftime(todayBuf, sizeof(todayBuf), "%Y%m%d", localToday);
	    long todayIntKey = std::atol(todayBuf);
	
	    std::string configDirectoryPath = "/boot/home/config/settings/HaikuDVR";
	    DIR* dir = opendir(configDirectoryPath.c_str());
	    if (dir != nullptr) {
	        struct dirent* entry;
	        uint32 deletedFilesCount = 0;
	
	        while ((entry = readdir(dir)) != nullptr) {
	            std::string filename(entry->d_name);
	            
	            // Check for file mask pattern 'guide_YYYYMMDD' (length is exactly 14 characters)
	            if (filename.rfind("guide_", 0) == 0 && filename.length() == 14) { 
	                std::string datePart = filename.substr(6, 8);
	                
	                if (datePart.find_first_not_of("0123456789") == std::string::npos) {
	                    long fileDateIntKey = std::atol(datePart.c_str());
	                    
	                    if (fileDateIntKey < todayIntKey) {
	                        std::string fullPathToDelete = configDirectoryPath + "/" + filename;
	                        if (unlink(fullPathToDelete.c_str()) == 0) {
	                            deletedFilesCount++;
	                        }
	                    }
	                }
	            }
	        }
	        closedir(dir);
	        std::printf("[DVR PURGE] Storage sweep complete. Purged %u obsolete history file chunks.\n", deletedFilesCount);
	    }
	    // =========================================================================
	
	    // Update the timestamp log in guide_master.xml.cache so the timer safely resets
	    std::ofstream cacheOut(cacheControlPath);
	    if (cacheOut.is_open()) {
	        cacheOut << currentTime << "\n";
	        cacheOut.close();
	    }
	
	    return true; 
	}


// Static network payload callback wrapper to capture text vectors safely over cURL
static size_t NetworkStringCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    std::string* s = static_cast<std::string*>(userp);
    size_t totalSize = size * nmemb;
    s->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

// Complete combined asynchronous schedule downloader thread loop
static int32 AsyncUpdateGuideWorker(void* data) {
    if (gFrontendDebugEnable) {
        std::printf("[GUIDE TRACKER] Asynchronous background update sync worker thread initialized.\n");
        std::fflush(stdout);
    }

    // 1. DYNAMIC NETWORK TUNER IDENTIFICATION PASS
    std::string targetIp = "";
    std::vector<std::string> tuners = DiscoverAllTuners();
    if (!tuners.empty()) {
        targetIp = tuners[0];
    } else {
        std::printf("[GUIDE TRACKER ERROR] Sync aborted. No hardware tuners found on subnet.\n");
        std::fflush(stdout);
        return B_ERROR;
    }

    // 2. DISCOVER LOCAL DEVICE AUTHENTICATION TOKEN (discover.json)
    std::string discoveryUrl = "http://" + targetIp + "/discover.json";
    std::string discoverPayload = "";
    
    if (gFrontendDebugEnable) {
        std::printf("[GUIDE TRACKER] Querying device profile metrics via: %s\n", discoveryUrl.c_str());
        std::fflush(stdout);
    }

    CURL* curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 HaikuDVR/1.0");
        curl_easy_setopt(curl, CURLOPT_URL, discoveryUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NetworkStringCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &discoverPayload);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L); // 5-second timeout safeguard
        
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        
        if (res != CURLE_OK) {
            std::printf("[GUIDE TRACKER ERROR] Failed to connect to local tuner hardware discovery profile endpoint.\n");
            std::fflush(stdout);
            return B_ERROR;
        }
    }

    std::string deviceAuthToken = "";
    try {
        auto jDisc = json::parse(discoverPayload);
        if (jDisc.is_object() && jDisc.contains("DeviceAuth")) {
            deviceAuthToken = jDisc["DeviceAuth"].get<std::string>();
        }
    } catch (...) {
        std::printf("[GUIDE TRACKER ERROR] Failed to parse JSON dictionary tokens from discovery file payload.\n");
        std::fflush(stdout);
        return B_ERROR;
    }

    if (deviceAuthToken.empty()) {
        std::printf("[GUIDE TRACKER ERROR] Tuner device reported an empty or unauthorized DeviceAuth token parameter key.\n");
        std::fflush(stdout);
        return B_ERROR;
    }

    // 3. GENERATE TARGET INFRASTRUCTURE FILENAMES AND ENDPOINT LINKS
    std::string xmltvUrl = "https://api.hdhomerun.com/api/xmltv?DeviceAuth=" + deviceAuthToken;
    std::string masterXmlPath = "/boot/home/config/settings/HaikuDVR/guide_master.xml";

    if (gFrontendDebugEnable) {
        std::printf("[GUIDE TRACKER] Token match success! Running upstream cURL channel download payload from cloud link: %s\n", xmltvUrl.c_str());
        std::fflush(stdout);
    }

    // 4. DOWNSTREAM MASTER SECURE TRANSMISSION
    std::FILE* xmlFilePtr = std::fopen(masterXmlPath.c_str(), "wb");
    if (!xmlFilePtr) {
        std::printf("[GUIDE TRACKER ERROR] Failed to open master XML destination handle file configuration on system disk storage filesystem.\n");
        std::fflush(stdout);
        return B_ERROR;
    }

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, xmltvUrl.c_str());
        // Mirroring your exact user-agent string signature parameters
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 HaikuDVR/1.0");
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip"); // Enforces lightning fast hardware link compression pipelines
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, xmlFilePtr);
        curl_easy_setopt(curl, CURLOPT_HEADER, 0L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L); // 1-minute streaming time threshold window
        
        CURLcode res = curl_easy_perform(curl);
        std::fclose(xmlFilePtr);
        curl_easy_cleanup(curl);

        if (res == CURLE_OK) {
            if (gFrontendDebugEnable) {
                std::printf("[GUIDE TRACKER] XML Schedule download completed successfully. Initializing local database processing block tables...\n");
                std::fflush(stdout);
            }

            // 5. PROCESS XML MATRIX STREAM DATA DIRECTLY ONTO SQLITE STORAGE
            bool ingestSuccess = IngestMasterXmlToSqlite(masterXmlPath);
            
            if (ingestSuccess && gFrontendDebugEnable) {
                std::printf("[GUIDE TRACKER SUCCESS] System database tables synchronized cleanly! Background thread safe-closing.\n");
                std::fflush(stdout);
            }
        } else {
            std::printf("[GUIDE TRACKER ERROR] SiliconDust secure cloud connection transmission collapsed: %s\n", curl_easy_strerror(res));
            std::fflush(stdout);
            return B_ERROR;
        }
    }

    return B_OK;
}



    // Endpoint: GET /api/guide — Queries your live SQLite guide.db directly
	void HandleGetTvGuide(int clientFd, const std::string& requestStr) {
	    // RUN AUTOMATED DYNAMIC CACHE LOGIC ON EVERY WEB QUERY
	    bool dataNeedsSync = VerifyAndRefreshGuideCache();
	    if (dataNeedsSync) {
	        if (gFrontendDebugEnable) {
	            std::printf("[DVR INTERFACE] Notice: System needs to trigger an external upstream XML schedule download sync.\n");
	            std::fflush(stdout);
	        }
	
	        // --- SPAWN THE ASYNC WORKER THREAD ---
	        // Uses Haiku OS native thread spawning primitives to avoid locking up your web dashboard
	        thread_id syncThread = spawn_thread(AsyncUpdateGuideWorker, "DVR_GuideSync_Worker", B_NORMAL_PRIORITY, nullptr);
	        if (syncThread >= 0) {
	            resume_thread(syncThread);
	        } else {
	            std::printf("[DVR INTERFACE ERROR] Failed to spawn background guide update sync worker thread.\n");
	            std::fflush(stdout);
	        }
	    }
	
	    std::string targetDate = "";
	    std::string targetTime = "";   

        
        // 1. Safe extraction of ?dt= and &tm= parameters
        size_t dtPos = requestStr.find("dt=");
        if (dtPos != std::string::npos) {
            targetDate = requestStr.substr(dtPos + 3, 10); // Extracted YYYY-MM-DD
        }
        
        size_t tmPos = requestStr.find("tm=");
        if (tmPos != std::string::npos) {
            // Find end of parameter value (space or end of string)
            size_t tmEnd = requestStr.find(" ", tmPos);
            std::string rawTime = (tmEnd != std::string::npos) ? 
                                  requestStr.substr(tmPos + 3, tmEnd - (tmPos + 3)) : 
                                  requestStr.substr(tmPos + 3);
            
            // Clean URL-encoded colon sequences (%3A / %3a)
            for (size_t i = 0; i < rawTime.length(); i++) {
                if (rawTime[i] == '%' && i + 2 < rawTime.length() && 
                    (rawTime[i+1] == '3' && (rawTime[i+2] == 'A' || rawTime[i+2] == 'a'))) {
                    targetTime += ":";
                    i += 2;
                } else {
                    targetTime += rawTime[i];
                }
            }
            if (targetTime.length() > 5) targetTime = targetTime.substr(0, 5); // Safe truncate to HH:MM
        }

        sqlite3* db = nullptr;
        const char* dbPath = "/boot/home/config/settings/HaikuDVR/guide.db";
        
        if (sqlite3_open(dbPath, &db) != SQLITE_OK) {
            if (db) sqlite3_close(db);
            json err = {{"status", "error"}, {"message", "Could not open guide database."}};
            SendJsonResponse(clientFd, 500, "Internal Server Error", err.dump());
            return;
        }

        // 2. FIXED TIME CONDITION: Treat incoming string parameters explicitly as 'localtime'
        std::string timeFilter = "p.end_epoch > strftime('%s', 'now') ";
        if (!targetDate.empty() && targetTime.length() == 5) {
            std::string fullTargetStr = targetDate + " " + targetTime + ":00";
            timeFilter = "p.start_epoch <= strftime('%s', '" + fullTargetStr + "', 'utc') "
                         "AND p.end_epoch > strftime('%s', '" + fullTargetStr + "', 'utc') ";
        }

        std::string sql = 
            "WITH RankedPrograms AS ("
            "    /* FIXED: Added p.end_epoch to the interior subquery select sequence */\n"
            "    SELECT p.title, c.lcn, p.desc, p.start_epoch, p.end_epoch, "
            "           strftime('%H:%M', p.start_epoch, 'unixepoch', 'localtime') as start_t, "
            "           strftime('%H:%M', p.end_epoch, 'unixepoch', 'localtime') as end_t, "
            "           strftime('%Y-%m-%d', p.start_epoch, 'unixepoch', 'localtime') as air_date, "
            "           ROW_NUMBER() OVER (PARTITION BY p.channel_id ORDER BY p.start_epoch ASC) as rn "
            "    FROM programs p "
            "    LEFT JOIN channels c ON p.channel_id = c.xml_id "
            "    WHERE " + timeFilter + 
            ") "
            "SELECT title, lcn, start_t, end_t, desc, air_date, start_epoch, end_epoch "
            "FROM RankedPrograms "
            "WHERE rn = 1 "
            "ORDER BY "
            "         CAST(CASE WHEN instr(lcn, '.') > 0 THEN substr(lcn, 1, instr(lcn, '.') - 1) ELSE lcn END AS INTEGER) ASC, "
            "         CAST(CASE WHEN instr(lcn, '.') > 0 THEN substr(lcn, instr(lcn, '.') + 1) ELSE 0 END AS INTEGER) ASC;";



        sqlite3_stmt* stmt = nullptr;
        json jResults = json::array();

        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* title   = (const char*)sqlite3_column_text(stmt, 0);
            const char* lcn     = (const char*)sqlite3_column_text(stmt, 1);
            const char* start   = (const char*)sqlite3_column_text(stmt, 2);
            const char* end     = (const char*)sqlite3_column_text(stmt, 3);
            const char* desc    = (const char*)sqlite3_column_text(stmt, 4); 
            const char* airDate = (const char*)sqlite3_column_text(stmt, 5); 

            int64_t startEpoch = (int64_t)sqlite3_column_int64(stmt, 6);
            int64_t endEpoch   = (int64_t)sqlite3_column_int64(stmt, 7);
            int64_t durationSec = endEpoch - startEpoch;

            // FIXED: Clean title and description strings completely before packaging
            std::string cleanTitle = title ? DecodeHtmlEntities(title) : "Unknown";
            std::string cleanDesc  = desc ? DecodeHtmlEntities(desc) : "No description available.";
            std::string sLcn = lcn ? lcn : "??";
            std::string sAirDate = airDate ? airDate : "";
            std::string sStart = start ? start : "--:--";

            // Maintain your gScheduleList cross-matching tuner loops normally here...
            std::string pairedTunerIp = ""; 
            gScheduleLocker.Lock();
            for (const auto& item : gScheduleList) {
                if (item.channel == sLcn && item.startDate == sAirDate && item.startTime == sStart) {
                    pairedTunerIp = item.tunerIp;
                    break;
                }
            }
            gScheduleLocker.Unlock();

            jResults.push_back({
                {"title", cleanTitle},
                {"channel", sLcn},
                {"start_time", sStart},
                {"end_time", end ? end : "--:--"},
                {"description", cleanDesc}, // Pure human-readable quotation characters
                {"show_description", cleanDesc},
                {"air_date", sAirDate},
                {"duration", durationSec},
                {"tuner_ip", pairedTunerIp}
            });
        }

        }

        sqlite3_finalize(stmt);
        sqlite3_close(db);
        SendJsonResponse(clientFd, 200, "OK", jResults.dump());
    }


    // Endpoint: GET /api/search?q=... — Dynamic asynchronous database filtering
    void HandleApiSearch(int clientFd, const std::string& requestStr) {
        size_t queryPos = requestStr.find("/api/search?q=");
        if (queryPos == std::string::npos) {
            SendJsonResponse(clientFd, 400, "Bad Request", "{\"error\":\"Missing query parameter\"}");
            return;
        }
        
        size_t endPos = requestStr.find(" ", queryPos);
        std::string rawQuery = requestStr.substr(queryPos + 14, endPos - (queryPos + 14));
        
        // Simple URL Decode helper to turn %20 tokens back to spaces for SQLite matching
        std::string cleanedQuery = "";
        for (size_t i = 0; i < rawQuery.length(); i++) {
            if (rawQuery[i] == '%' && i + 2 < rawQuery.length()) {
                char chr = static_cast<char>(std::strtol(rawQuery.substr(i + 1, 2).c_str(), nullptr, 16));
                cleanedQuery += chr; i += 2;
            } else if (rawQuery[i] == '+') { cleanedQuery += ' '; }
            else { cleanedQuery += rawQuery[i]; }
        }

        sqlite3* db = nullptr;
        if (sqlite3_open("/boot/home/config/settings/HaikuDVR/guide.db", &db) != SQLITE_OK) {
            if (db) sqlite3_close(db);
            SendJsonResponse(clientFd, 500, "Error", "{\"error\":\"Database open failed\"}");
            return;
        }

           const char* sql = 
            "SELECT p.title, c.lcn, "
            "       strftime('%H:%M', p.start_epoch, 'unixepoch', 'localtime') as start_t, "
            "       strftime('%H:%M', p.end_epoch, 'unixepoch', 'localtime') as end_t, "
            "       p.desc, "
            "       strftime('%Y-%m-%d', p.start_epoch, 'unixepoch', 'localtime') as air_date, "
            "       p.start_epoch, p.end_epoch " 
            "FROM programs p "
            "LEFT JOIN channels c ON p.channel_id = c.xml_id "
            "WHERE lower(p.title) LIKE lower(?) "
            "  AND p.end_epoch > strftime('%s', 'now') /* Guard: Don't return past shows */ "
            "ORDER BY "
            "         /* 1. Sort by major channel digits numerically */ "
            "         CAST(CASE "
            "           WHEN instr(c.lcn, '.') > 0 THEN substr(c.lcn, 1, instr(c.lcn, '.') - 1) "
            "           ELSE c.lcn "
            "         END AS INTEGER) ASC, "
            "         /* 2. Sort by minor sub-channel digits numerically */ "
            "         CAST(CASE "
            "           WHEN instr(c.lcn, '.') > 0 THEN substr(c.lcn, instr(c.lcn, '.') + 1) "
            "           ELSE 0 "
            "         END AS INTEGER) ASC, "
            "         /* 3. Chronological sorting for different episodes on the same station */ "
            "         p.start_epoch ASC "
            "LIMIT 50;";

        sqlite3_stmt* stmt = nullptr;
        json jResults = json::array();

        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            std::string bindPattern = "%" + cleanedQuery + "%";
            sqlite3_bind_text(stmt, 1, bindPattern.c_str(), -1, SQLITE_TRANSIENT);

            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* title   = (const char*)sqlite3_column_text(stmt, 0);
                const char* lcn     = (const char*)sqlite3_column_text(stmt, 1);
                const char* start   = (const char*)sqlite3_column_text(stmt, 2);
                const char* end     = (const char*)sqlite3_column_text(stmt, 3);
                const char* desc    = (const char*)sqlite3_column_text(stmt, 4); 
                const char* airDate = (const char*)sqlite3_column_text(stmt, 5); 

                int64_t startEpoch  = (int64_t)sqlite3_column_int64(stmt, 6);
                int64_t endEpoch    = (int64_t)sqlite3_column_int64(stmt, 7);
                int64_t durationSec = endEpoch - startEpoch;

                // FIXED: Clean your text fields before serializing down to array models
                std::string cleanTitle = title ? DecodeHtmlEntities(title) : "Unknown";
                std::string cleanDesc  = desc ? DecodeHtmlEntities(desc) : "No description available.";

                jResults.push_back({
                    {"title", cleanTitle},
                    {"channel", lcn ? lcn : "??"},
                    {"start_time", start ? start : "--:--"},
                    {"end_time", end ? end : "--:--"},
                    {"description", cleanDesc},
                    {"air_date", airDate ? airDate : ""},
                    {"duration", durationSec} 
                });
            }

        }

        sqlite3_finalize(stmt);
        sqlite3_close(db);
        SendJsonResponse(clientFd, 200, "OK", jResults.dump(4));
    }


    // Endpoint: GET /api/tuners — Dynamically pulls live HDHomeRun IPs via libhdhomerun
    void HandleGetTuners(int clientFd) {
        // Query your existing device discovery library wrapper
        std::vector<std::string> discoveredList = DiscoverAllTuners();
        
        json jTuners = json::array();
        for (const auto& tunerIp : discoveredList) {
            jTuners.push_back(tunerIp);
        }

        // Output JSON back to frontend with wide CORS allowances
        SendJsonResponse(clientFd, 200, "OK", jTuners.dump());
    }


    // Endpoint: GET /api/schedules — Exports current memory schedules as JSON strings
    void HandleGetSchedules(int clientFd) {
        gScheduleLocker.Lock();
        json jSchedules = json::array();
        for (const auto& item : gScheduleList) {
            jSchedules.push_back({
                {"date", item.startDate},
                {"time", item.startTime},
                {"channel", item.channel},
                {"duration", item.duration},
                {"processed", item.processed},
                {"tuner_ip", item.tunerIp},
                {"show_title", item.showTitle},
                {"description", item.showDescription},    
    			{"show_description", item.showDescription} 
            });
        }
        gScheduleLocker.Unlock();
        SendJsonResponse(clientFd, 200, "OK", jSchedules.dump(4));
    }

	// Endpoint: POST /api/schedules/add — Adds a new task over the network
	void HandleAddSchedule(int clientFd, const std::string& requestStr) {
	    std::string body = ExtractHttpRequestBody(requestStr);
	    try {
	        json jIn = json::parse(body);
	        ScheduleItem item;
	        item.startDate    = jIn.at("date").get<std::string>();
	        item.startTime    = jIn.at("time").get<std::string>();
	        item.channel      = jIn.at("channel").get<std::string>();
	        item.duration     = jIn.at("duration").get<std::string>();
		
			item.tunerIp = jIn.value("tuner_ip", "");			
			if (item.tunerIp.empty()) {
			    std::vector<std::string> liveTuners = DiscoverAllTuners();
			    if (!liveTuners.empty()) {
			        item.tunerIp = liveTuners[0]; 
			        if (gFrontendDebugEnable) {
			            std::printf("[SCHEDULE AUTO-DETECT] Resolved empty tuner_ip to live unit: %s\n", item.tunerIp.c_str());
			        }
			    } else {
			        if (gFrontendDebugEnable) {
			            std::printf("[SCHEDULE WARNING] No live tuners found on network. Please Fix.");
			        }
			    }
			}

	        item.showTitle    = jIn.value("title", "Remote Web Record");
	        item.showDescription = jIn.value("description", ""); 
	        item.processed    = 0; 
	        
	        item.durationSec  = std::atoll(item.duration.c_str());
	        item.epochStart   = CalculateEpoch(item.startDate, item.startTime);
	
	        gScheduleLocker.Lock();
	        gScheduleList.push_back(item);
	        gScheduleLocker.Unlock();
	        
	        SaveSchedulesToDisk(); // Serializes straight down to your persistent JSON file
	
	        json resp = {{"status", "success"}, {"message", "Schedule appended successfully"}};
	        SendJsonResponse(clientFd, 201, "Created", resp.dump());
	    } catch (const std::exception& e) {
	        json errorJson = {{"status", "error"}, {"message", std::string("JSON Validation Fail: ") + e.what()}};
	        SendJsonResponse(clientFd, 400, "Bad Request", errorJson.dump());
	    }
	}


 
    void ServeScpd(int clientFd) {
        // Industry-standard UPnP ContentDirectory definition block tracking the "Browse" action
        std::string xml = 
            "<?xml version=\"1.0\"?>\r\n"
            "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\r\n"
            "  <specVersion><major>1</major><minor>0</minor></specVersion>\r\n"
            "  <actionList>\r\n"
            "    <action>\r\n"
            "      <name>Browse</name>\r\n"
            "      <argumentList>\r\n"
            "        <argument>\r\n"
            "          <name>ObjectID</name>\r\n"
            "          <direction>in</direction>\r\n"
            "          <relatedStateVariable>A_ARG_TYPE_ObjectID</relatedStateVariable>\r\n"
            "        </argument>\r\n"
            "        <argument>\r\n"
            "          <name>BrowseFlag</name>\r\n"
            "          <direction>in</direction>\r\n"
            "          <relatedStateVariable>A_ARG_TYPE_BrowseFlag</relatedStateVariable>\r\n"
            "        </argument>\r\n"
            "        <argument>\r\n"
            "          <name>Filter</name>\r\n"
            "          <direction>in</direction>\r\n"
            "          <relatedStateVariable>A_ARG_TYPE_Filter</relatedStateVariable>\r\n"
            "        </argument>\r\n"
            "        <argument>\r\n"
            "          <name>StartingIndex</name>\r\n"
            "          <direction>in</direction>\r\n"
            "          <relatedStateVariable>A_ARG_TYPE_Index</relatedStateVariable>\r\n"
            "        </argument>\r\n"
            "        <argument>\r\n"
            "          <name>RequestedCount</name>\r\n"
            "          <direction>in</direction>\r\n"
            "          <relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable>\r\n"
            "        </argument>\r\n"
            "        <argument>\r\n"
            "          <name>SortCriteria</name>\r\n"
            "          <direction>in</direction>\r\n"
            "          <relatedStateVariable>A_ARG_TYPE_SortCriteria</relatedStateVariable>\r\n"
            "        </argument>\r\n"
            "        <argument>\r\n"
            "          <name>Result</name>\r\n"
            "          <direction>out</direction>\r\n"
            "          <relatedStateVariable>A_ARG_TYPE_Result</relatedStateVariable>\r\n"
            "        </argument>\r\n"
            "        <argument>\r\n"
            "          <name>NumberReturned</name>\r\n"
            "          <direction>out</direction>\r\n"
            "          <relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable>\r\n"
            "        </argument>\r\n"
            "        <argument>\r\n"
            "          <name>TotalMatches</name>\r\n"
            "          <direction>out</direction>\r\n"
            "          <relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable>\r\n"
            "        </argument>\r\n"
            "        <argument>\r\n"
            "          <name>UpdateID</name>\r\n"
            "          <direction>out</direction>\r\n"
            "          <relatedStateVariable>A_ARG_TYPE_UpdateID</relatedStateVariable>\r\n"
            "        </argument>\r\n"
            "      </argumentList>\r\n"
            "    </action>\r\n"
            "  </actionList>\r\n"
            "  <serviceStateTable>\r\n"
            "    <stateVariable sendEvents=\"no\">\r\n"
            "      <name>A_ARG_TYPE_ObjectID</name>\r\n"
            "      <dataType>string</dataType>\r\n"
            "    </stateVariable>\r\n"
            "    <stateVariable sendEvents=\"no\">\r\n"
            "      <name>A_ARG_TYPE_BrowseFlag</name>\r\n"
            "      <dataType>string</dataType>\r\n"
            "    </stateVariable>\r\n"
            "    <stateVariable sendEvents=\"no\">\r\n"
            "      <name>A_ARG_TYPE_Filter</name>\r\n"
            "      <dataType>string</dataType>\r\n"
            "    </stateVariable>\r\n"
            "    <stateVariable sendEvents=\"no\">\r\n"
            "      <name>A_ARG_TYPE_Index</name>\r\n"
            "      <dataType>ui4</dataType>\r\n"
            "    </stateVariable>\r\n"
            "    <stateVariable sendEvents=\"no\">\r\n"
            "      <name>A_ARG_TYPE_Count</name>\r\n"
            "      <dataType>ui4</dataType>\r\n"
            "    </stateVariable>\r\n"
            "    <stateVariable sendEvents=\"no\">\r\n"
            "      <name>A_ARG_TYPE_SortCriteria</name>\r\n"
            "      <dataType>string</dataType>\r\n"
            "    </stateVariable>\r\n"
            "    <stateVariable sendEvents=\"no\">\r\n"
            "      <name>A_ARG_TYPE_Result</name>\r\n"
            "      <dataType>string</dataType>\r\n"
            "    </stateVariable>\r\n"
            "    <stateVariable sendEvents=\"no\">\r\n"
            "      <name>A_ARG_TYPE_UpdateID</name>\r\n"
            "      <dataType>ui4</dataType>\r\n"
            "    </stateVariable>\r\n"
            "  </serviceStateTable>\r\n"
            "</scpd>\r\n";

        std::string resp = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/xml; charset=\"utf-8\"\r\n"
            "Content-Length: " + std::to_string(xml.length()) + "\r\n"
            "Connection: close\r\n\r\n" + xml;

        send(clientFd, resp.c_str(), resp.length(), 0);
    }

    void ServeDescription(int clientFd) {
        // Automatically determine the correct names and UUIDs at compile time
        #if defined(__LP64__) || defined(_LP64) || defined(__x86_64__)
            // 64-bit Architecture Configuration
            const char* friendlyName = "Haiku 64 DVR Server";
            const char* udnUuid = "uuid:fe80::haiku:dvr-64bit";
        #else
            // 32-bit Architecture Configuration (fallback)
            const char* friendlyName = "Haiku 32bit DVR Server";
            const char* udnUuid = "uuid:fe80::haiku:dvr-32bit";
        #endif

        // Build the XML payload dynamically using string formatting
        char xmlBuffer[2048];
        std::snprintf(xmlBuffer, sizeof(xmlBuffer),
            "<?xml version=\"1.0\"?>\r\n"
            "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">\r\n"
            "  <specVersion><major>1</major><minor>0</minor></specVersion>\r\n"
            "  <device>\r\n"
            "    <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>\r\n"
            "    <friendlyName>%s</friendlyName>\r\n"
            "    <manufacturer>Haiku Community</manufacturer>\r\n"
            "    <modelName>HaikuDVR</modelName>\r\n"
            "    <UDN>%s</UDN>\r\n"
            "    <serviceList>\r\n"
            "      <service>\r\n"
            "        <serviceType>urn:schemas-upnp-org:service:ContentDirectory:1</serviceType>\r\n"
            "        <serviceId>urn:upnp-org:serviceId:ContentDirectory</serviceId>\r\n"
            "        <controlURL>/ContentDirectory/control</controlURL>\r\n"
            "        <eventSubURL>/ContentDirectory/event</eventSubURL>\r\n"
            "        <SCPDURL>/ContentDirectory/scpd.xml</SCPDURL>\r\n"
            "      </service>\r\n"
            "    </serviceList>\r\n"
            "  </device>\r\n"
            "</root>\r\n",
            friendlyName, udnUuid
        );

        std::string xml(xmlBuffer);

        std::string resp = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/xml; charset=\"utf-8\"\r\n"
            "Content-Length: " + std::to_string(xml.length()) + "\r\n"
            "Connection: close\r\n\r\n" + xml;

        send(clientFd, resp.c_str(), resp.length(), 0);
    }



    // Helper function to escape reserved XML control characters
    std::string EscapeXml(const std::string& data) {
        std::string buffer;
        buffer.reserve(data.size());
        for (size_t pos = 0; pos != data.size(); ++pos) {
            switch (data[pos]) {
                case '&':  buffer.append("&amp;");       break;
                case '\"': buffer.append("&quot;");      break;
                case '\'': buffer.append("&apos;");      break;
                case '<':  buffer.append("&lt;");        break;
                case '>':  buffer.append("&gt;");        break;
                default:   buffer.append(&data[pos], 1); break;
            }
        }
        return buffer;
    }
    
    




	std::string CleanFilename(const std::string& rawName) {
	    std::string clean = rawName;
	    
	    // Match prefix: "DVR_Record_Ch_" followed by channel, date, and time patterns
	    // e.g., "DVR_Record_Ch_2.1_2026-06-28_19-30_"
	    std::regex prefixRegex("^DVR_Record_Ch_\\d+\\.\\d+_\\d{4}-\\d{2}-\\d{2}_\\d{2}-\\d{2}_");
	    clean = std::regex_replace(clean, prefixRegex, "");
	    
	    // Match suffix: recording duration, padded tag, and file extension
	    // e.g., "_12480s_Padded.ts" or "_7200s_Padded.ts"
	    std::regex suffixRegex("_\\d+s_Padded\\.(ts|mpg)$");
	    clean = std::regex_replace(clean, suffixRegex, "");
	    
	    // Replace remaining underscores with spaces for human readability
	    std::replace(clean.begin(), clean.end(), '_', ' ');
	    
	    return clean;
	}
	
	
	std::string UrlEncodeFilename(const std::string& value) {
	    std::ostringstream escaped;
	    escaped << std::hex << std::uppercase;
	    for (char c : value) {
	        // Safe unreserved URL characters
	        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
	            escaped << c;
	        } else {
	            // Converts characters like spaces to %20 and single quotes to %27
	            escaped << '%' << std::setw(2) << std::setfill('0') << (static_cast<int>(c) & 0xFF);
	        }
	    }
	    return escaped.str();
	}



    void ServeContentDirectory(int clientFd, const std::string& requestStr) {
        if (gFrontendDebugEnable) {
            std::printf("\n==================================================\n");
            std::printf("[DLNA DEBUG] Media Client requested folder data parsing!\n");
        }
        
        if (rootDir.empty()) {
            rootDir = "/boot/home/Recordings";
        }
        std::fflush(stdout);

        std::string targetObjectId = "0";
        if (requestStr.find("<ObjectID>3</ObjectID>") != std::string::npos || 
            requestStr.find("ObjectID=\"3\"") != std::string::npos ||
            requestStr.find(">3<") != std::string::npos) {
            targetObjectId = "3";
        }

        if (gFrontendDebugEnable) {
            std::printf("[DLNA DEBUG] TV Client is browsing ObjectID: %s\n", targetObjectId == "0" ? "0 (Root Tree)" : "3 (Videos Folder)");
            std::fflush(stdout);
        }

        std::string didlBody = 
            "<DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" "
            "xmlns:dc=\"http://purl.org\" "
            "xmlns:r=\"urn:schemas-rinconnetworks-com:metadata-1-0/\" "
            "xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\">";
        
        int itemsReturned = 0;

        if (targetObjectId == "0") {
            int actualVideoCount = 0;
            DIR* countDir = opendir(rootDir.c_str());
            if (countDir) {
                struct dirent* entry;
                while ((entry = readdir(countDir)) != nullptr) {
                    std::string name(entry->d_name);
                    if (name.find(".ts") != std::string::npos || name.find(".mpg") != std::string::npos) {
                        actualVideoCount++;
                    }
                }
                closedir(countDir);
            }

            didlBody += "<container id=\"3\" parentID=\"0\" childCount=\"" + std::to_string(actualVideoCount) + "\" restricted=\"1\">"
                        "<dc:title>Recordings</dc:title>"
                        "<upnp:class>object.container.storageFolder</upnp:class>"
                        "</container>";
            itemsReturned++;
            
        } else {
            int trackId = 100; 
            DIR* dir = opendir(rootDir.c_str());
            if (dir) {
                struct dirent* entry;
                while ((entry = readdir(dir)) != nullptr) {
                    std::string name(entry->d_name);
                    
                    if (name.find(".ts") != std::string::npos || name.find(".mpg") != std::string::npos) {
                        
                        // Percent-encode ONLY the filename part of the URL string
                        std::string encodedName = UrlEncodeFilename(name);
                        std::string streamUrl = "http://" + localIp + ":" + std::to_string(port) + "/video/" + encodedName;
                        
                        if (gFrontendDebugEnable) {
                            std::printf("  -> MATCH: Listing file inside folder 3: %s\n", name.c_str());
                            std::printf("     URLEncoded Stream Link: %s\n", streamUrl.c_str());
                            std::fflush(stdout);
                        }
                        
                        // Parse the raw filename to present a clean visual title to the DLNA client
                        std::string prettyName = CleanFilename(name);
                        
                        // Dynamic profile structure declaring exact North American MPEG-TS feeds
                        std::string dlnaProfile = "http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_TS_HD_NA_ISO;DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=01700000000000000000000000000000";

                        didlBody += "<item id=\"" + std::to_string(trackId++) + "\" parentID=\"3\" restricted=\"1\">"
                                    "<dc:title>" + prettyName + "</dc:title>" 
                                    "<upnp:class>object.item.videoItem.movie</upnp:class>"
                                    "<res protocolInfo=\"" + dlnaProfile + "\">" + streamUrl + "</res>" 
                                    "</item>";
                        itemsReturned++;
                    }
                }
                closedir(dir);
            }
        }


        didlBody += "</DIDL-Lite>";

        std::string escapedDidl = EscapeXml(didlBody);

        std::string soap = 
            "<s:Envelope xmlns:s=\"http://xmlsoap.org\" "
            "s:encodingStyle=\"http://xmlsoap.org\">"
            "<s:Body><u:BrowseResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">"
            "<Result>" + escapedDidl + "</Result>"
            "<NumberReturned>" + std::to_string(itemsReturned) + "</NumberReturned>"
            "<TotalMatches>" + std::to_string(itemsReturned) + "</TotalMatches>"
            "<UpdateID>1</UpdateID>"
            "</u:BrowseResponse></s:Body></s:Envelope>";

        std::string resp = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/xml; charset=\"utf-8\"\r\n"
            "Content-Length: " + std::to_string(soap.length()) + "\r\n"
            "Connection: close\r\n\r\n" + soap;

        if (gFrontendDebugEnable) {
            std::printf("[DLNA DEBUG] Transmitting hierarchical asset block size: %zu bytes\n", resp.length());
            std::printf("==================================================\n\n");
            std::fflush(stdout);
        }

        send(clientFd, resp.c_str(), resp.length(), 0);
    }




	// Helper utility to convert %27 back to "'" and %20 back to spaces
	std::string UrlDecodeFilename(const std::string& value) {
	    std::string result;
	    result.reserve(value.length());
	    
	    for (size_t i = 0; i < value.length(); ++i) {
	        if (value[i] == '%' && i + 2 < value.length()) {
	            std::string hexStr = value.substr(i + 1, 2);
	            char chr = static_cast<char>(std::strtol(hexStr.c_str(), nullptr, 16));
	            result += chr;
	            i += 2; 
	        } else if (value[i] == '+') {
	            result += ' '; 
	        } else {
	            result += value[i];
	        }
	    }
	    return result;
	}
	
    void StreamVideoFile(int clientFd, const std::string& req) {
        size_t pos = req.find("/video/");
        size_t endPos = req.find(" ", pos);
        if (pos == std::string::npos || endPos == std::string::npos) {
            close(clientFd); // Clean up right away on a malformed path
            return;
        }
        
        std::string rawFilename = req.substr(pos + 7, endPos - (pos + 7));
        std::string filename = UrlDecodeFilename(rawFilename);
        std::string fullPath = rootDir + "/" + filename;

        if (gFrontendDebugEnable) {
            std::printf("[STREAM] Reconstructed disk path: %s\n", fullPath.c_str());
            std::fflush(stdout);
        }

        std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            const char* notFound = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
            send(clientFd, notFound, std::strlen(notFound), 0);
            close(clientFd);
            return;
        }

        std::streamsize fileSize = file.tellg();
        std::streamsize startByte = 0;
        std::streamsize endByte = fileSize - 1;
        bool isPartial = false;

        size_t rangePos = req.find("Range: bytes=");
        if (rangePos != std::string::npos) {
            size_t startIdx = rangePos + 13;
            size_t dashIdx = req.find("-", startIdx);
            if (dashIdx != std::string::npos) {
                std::string startStr = req.substr(startIdx, dashIdx - startIdx);
                if (!startStr.empty()) {
                    startByte = std::atoll(startStr.c_str());
                    isPartial = true;
                }
                
                size_t nlnIdx = req.find("\r\n", dashIdx);
                if (nlnIdx != std::string::npos) {
                    std::string endStr = req.substr(dashIdx + 1, nlnIdx - (dashIdx + 1));
                    if (!endStr.empty()) {
                        endByte = std::atoll(endStr.c_str());
                    }
                }
            }
        }

        file.seekg(startByte, std::ios::beg);
        std::streamsize contentLength = (endByte - startByte) + 1;

        char header[1024] = {0}; 
        if (isPartial) {
            if (gFrontendDebugEnable) std::printf("[SEEK SYSTEM] Offset scrubbing position: %lld bytes\n", (long long)startByte);
            std::snprintf(header, sizeof(header),
                "HTTP/1.1 206 Partial Content\r\n"
                "Content-Type: video/mp2t\r\n"
                "Content-Range: bytes %lld-%lld/%lld\r\n"
                "Content-Length: %lld\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Connection: close\r\n\r\n", // DLNA seekers prefer individual close bursts
                (long long)startByte, (long long)endByte, (long long)fileSize, (long long)contentLength
            );
        } else {
            if (gFrontendDebugEnable) std::printf("[SEEK SYSTEM] Browser streaming from absolute beginning.\n");
            std::snprintf(header, sizeof(header),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: video/mp2t\r\n"
                "Content-Length: %lld\r\n"
                "Accept-Ranges: bytes\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Connection: keep-alive\r\n\r\n", // Keep-alive ensures the web canvas can stream continuously
                (long long)fileSize
            );
        }

        send(clientFd, header, std::strlen(header), 0);

        char chunk[64 * 1024]; 
        std::streamsize bytesRemaining = contentLength;

        while (bytesRemaining > 0 && file.good() && atomic_get(&gStopService) == 0) {
            std::streamsize toRead = sizeof(chunk);
            if (toRead > bytesRemaining) toRead = bytesRemaining;

            file.read(chunk, toRead);
            std::streamsize bytesRead = file.gcount();
            if (bytesRead <= 0) break;

            std::streamsize bytesSentTotal = 0;
            while (bytesSentTotal < bytesRead && atomic_get(&gStopService) == 0) {
                ssize_t sent = send(clientFd, chunk + bytesSentTotal, bytesRead - bytesSentTotal, 0);
                if (sent < 0) {
                    bytesRemaining = 0; 
                    break;
                }
                bytesSentTotal += sent;
                bytesRemaining -= sent;
            }
            if (bytesRemaining <= 0) break;
            snooze(100); // Tiny pause pacing prevents socket flooding on high bit-rate files
        }
        file.close();
        close(clientFd); // Method handles its own socket cleanup safely once delivery finishes

        if (gFrontendDebugEnable) {
            std::printf("[DLNA STREAM] Finished streaming media block to Client. Socket cleanly closing.\n");
            std::fflush(stdout);
        }
    }
};




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
    
    DlnasHttpStreamingServer gDlnaHttp;
    DlnasDiscoveryServer     gDlnaDiscovery; 
    
    void ReadyToRun() override {
        signal(SIGPIPE, SIG_IGN);
        LoadSchedulesFromDisk();
        
        std::string ip = GetHaikuLocalIpAddress(); 
        
        // THE PORT AUTO-FALLBACK: Start at 8081, increment if busy
        int port = 8081;
        while (port < 8090) {
            int testFd = socket(AF_INET, SOCK_STREAM, 0);
            if (testFd >= 0) {
                int reuse = 1;
                setsockopt(testFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
                
                struct sockaddr_in testAddr{};
                testAddr.sin_family = AF_INET;
                testAddr.sin_port = htons(port);
                testAddr.sin_addr.s_addr = INADDR_ANY;
                
                // If bind succeeds, the port is free!
                if (bind(testFd, (struct sockaddr*)&testAddr, sizeof(testAddr)) == 0) {
                    close(testFd);
                    break; 
                }
                close(testFd);
            }
            port++; // Port was busy, try the next one
        }

        gScheduleLocker.Lock();
        std::string safePathCopy = gGlobalSaveDirectory;
        gScheduleLocker.Unlock();

        // Start both engines using the validated safe port number
        gDlnaHttp.Start(port, safePathCopy, ip);
        gDlnaDiscovery.Start(ip, port);
        if (gFrontendDebugEnable) {
        	std::printf("[DVR BACKEND] DLNA Media Server initialized on http://%s:%d\n", ip.c_str(), port);
        	std::fflush(stdout); 
        }
        fLoopThread = spawn_thread(ServiceSchedulerLoop, "DVRServiceScheduler", B_LOW_PRIORITY, NULL);
        if (fLoopThread >= B_OK) {
            resume_thread(fLoopThread);
        }
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
	                   if (gFrontendDebugEnable) printf("[DVR SERVICE ERROR] Path key was NOT found in active worker map!\n");
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

void SignalExitInterceptor(int signalType) { 
    atomic_set(&gStopService, 1);
    atomic_set(&gCancelRecording, 1);
    
    DVRServiceApp* app = dynamic_cast<DVRServiceApp*>(be_app);
    if (app) {
        app->gDlnaDiscovery.Stop();
    }
}

int main() {
    DVRServiceApp app;
    app.Run();
    return 0;
}

